// The drawing of the start menu (start_menu.hpp). Everything on the screen comes from what the game already has: the `sm_screen` background of the original's Single / Multi
// screen (the orange tiles and the green frame), the ants of the setup screen's portraits, the game's TrueType face, and boxes and buttons that are made of the colours of
// the original's own button and box pictures (the pictures themselves carry their labels, so a button with a label of its own is built from the same palette: a one pixel
// outline, a face of the button's green, a light edge on the top and the left, a dark one on the bottom and the right, and the dark red drop shadow).

#include <algorithm>

#include "ants_app/start_menu.hpp"
#include "ants_app/text_layout.hpp"
#include "ants_app/ui_anim.hpp"
#include "ants_app/wide_page.hpp"

namespace ants::app {

namespace {

using assets::ColorRGBA;

// The palette of the original's buttons and boxes (read from the pictures of leave1, start1 and the setup screen's text boxes)
const ColorRGBA kOutline{7, 11, 15, 255};
const ColorRGBA kFace{43, 99, 87, 255};
const ColorRGBA kFaceHover{52, 124, 102, 255};
const ColorRGBA kFacePressed{35, 81, 71, 255};
const ColorRGBA kEdgeLight{115, 191, 155, 255};
const ColorRGBA kEdgeLight2{59, 151, 111, 255};
const ColorRGBA kEdgeDark{23, 83, 63, 255};
const ColorRGBA kEdgeDarker{19, 51, 35, 255};
const ColorRGBA kShadow{183, 11, 27, 255};
const ColorRGBA kBoxFrame{43, 95, 67, 255};
const ColorRGBA kBoxFrameDark{35, 71, 47, 255};
const ColorRGBA kOrange{219, 75, 19, 255};
const ColorRGBA kCream{239, 231, 223, 255};                // the colour of every label of the original's screens (0xdfe7ef)
const ColorRGBA kWhite{251, 251, 255, 255};
const ColorRGBA kBad{255, 112, 100, 255};
const ColorRGBA kDimOnBox{170, 182, 190, 255};
const ColorRGBA kInk{7, 11, 15, 255};                      // text on the orange: the black of the boxes
const ColorRGBA kInkDim{70, 24, 10, 255};

constexpr int32_t kPad = 8;

ColorRGBA ink_colour(MenuTone tone) {
    switch (tone) {
        case MenuTone::Dim: return kInkDim;
        case MenuTone::Bad: return ColorRGBA{119, 0, 0, 255};
        case MenuTone::Normal: break;
    }
    return kInk;
}

ColorRGBA box_text_colour(MenuTone tone) {
    switch (tone) {
        case MenuTone::Dim: return kDimOnBox;
        case MenuTone::Bad: return kBad;
        case MenuTone::Normal: break;
    }
    return kCream;
}

// A raised plate: the outline, the face, the light edge on the top and the left, the dark edge on the bottom and the right, the drop shadow
void draw_plate(IRenderer& r, const ButtonRect& b, const ColorRGBA& face, bool raised, bool bright) {
    const ColorRGBA light1 = raised ? (bright ? kWhite : kEdgeLight) : kEdgeDarker;
    const ColorRGBA light2 = raised ? kEdgeLight2 : kEdgeDark;
    const ColorRGBA dark1 = raised ? kEdgeDarker : kEdgeLight2;
    const ColorRGBA dark2 = raised ? kEdgeDark : kEdgeLight;
    r.fill_rect(b.x + 1, b.y + b.h, b.w, 1, kShadow);                              // the drop shadow: one pixel below and to the right
    r.fill_rect(b.x + b.w, b.y + 1, 1, b.h, kShadow);
    r.fill_rect(b.x, b.y, b.w, b.h, kOutline);
    r.fill_rect(b.x + 1, b.y + 1, b.w - 2, b.h - 2, face);
    r.fill_rect(b.x + 1, b.y + 1, b.w - 2, 1, light1);                             // top
    r.fill_rect(b.x + 1, b.y + 2, b.w - 2, 1, light2);
    r.fill_rect(b.x + 1, b.y + 1, 1, b.h - 2, light2);                             // left
    r.fill_rect(b.x + 1, b.y + b.h - 2, b.w - 2, 1, dark2);                        // bottom
    r.fill_rect(b.x + 1, b.y + b.h - 3, b.w - 2, 1, dark1);
    r.fill_rect(b.x + b.w - 2, b.y + 1, 1, b.h - 2, dark2);                        // right
}

// A dark box set into the screen (the setup screen's map name, info and status boxes): the black inside, the green frame, a light edge on the top and the left
void draw_inset(IRenderer& r, const ButtonRect& b, bool focused) {
    r.fill_rect(b.x + 1, b.y + b.h, b.w, 1, kShadow);
    r.fill_rect(b.x + b.w, b.y + 1, 1, b.h, kShadow);
    r.fill_rect(b.x, b.y, b.w, b.h, kBoxFrameDark);
    r.fill_rect(b.x + 1, b.y + 1, b.w - 2, b.h - 2, focused ? kEdgeLight2 : kFace);
    r.fill_rect(b.x + 1, b.y + 1, b.w - 2, 1, kEdgeLight2);
    r.fill_rect(b.x + 1, b.y + 1, 1, b.h - 2, kEdgeLight2);
    r.fill_rect(b.x + 1, b.y + b.h - 2, b.w - 2, 1, kBoxFrame);
    r.fill_rect(b.x + b.w - 2, b.y + 1, 1, b.h - 2, kBoxFrame);
    r.fill_rect(b.x + 3, b.y + 3, b.w - 6, b.h - 6, kInk);
}

// A text as the original's one-line labels cut it: cut at the right edge of the box
void draw_cut(IRenderer& r, const std::string& text, int32_t x, int32_t y, int32_t width, const ColorRGBA& colour, FontSize size, bool centered) {
    const std::string shown = fit_text(r, text, width, size);
    const int32_t w = r.get_text_width(shown, size);
    r.draw_text(shown, centered ? x + (width - w) / 2 : x, y, colour, size);
}

// A text centred in a box of its own height both ways
void draw_centered_line(IRenderer& r, const std::string& text, const ButtonRect& box, const ColorRGBA& colour, FontSize size, int32_t dy = 0) {
    const int32_t cell = font_cell_height(size);
    draw_cut(r, text, box.x + kPad, box.y + (box.h - cell) / 2 + dy, box.w - 2 * kPad, colour, size, true);
}

// A wrapped text in a box: lines of the original's wrap, as many as the box holds; what does not fit is cut (the last line that fits ends with "...")
void draw_wrapped(IRenderer& r, const std::string& text, const ButtonRect& box, const ColorRGBA& colour, FontSize size, bool centered, bool vcenter = false) {
    const int32_t pitch = font_cell_height(size);
    const int32_t max_lines = std::max(1, box.h / pitch);
    std::vector<std::string> lines = wrap_label_text(r, text, box.w, size);
    if (lines.size() > static_cast<size_t>(max_lines)) {
        lines.resize(static_cast<size_t>(max_lines));
        std::string& last = lines.back();
        while (!last.empty() && r.get_text_width(last + "...", size) > box.w) last.pop_back();
        last += "...";
    }
    int32_t y = vcenter ? box.y + std::max(0, (box.h - static_cast<int32_t>(lines.size()) * pitch) / 2) : box.y;
    for (const std::string& line : lines) {
        const std::string shown = fit_text(r, line, box.w, size);
        const int32_t w = r.get_text_width(shown, size);
        r.draw_text(shown, centered ? box.x + (box.w - w) / 2 : box.x, y, colour, size);
        y += pitch;
    }
}

// A small arrow of one pixel rows, pointing left or right (the cycler's, in the style of the original's arrow buttons)
void draw_arrow(IRenderer& r, bool left, int32_t cx, int32_t cy, const ColorRGBA& colour) {
    for (int32_t i = 0; i < 6; ++i) {
        const int32_t half = i;                                                    // the arrow is 6 wide and 11 high
        const int32_t x = left ? cx - 3 + i : cx + 2 - i;
        r.fill_rect(x, cy - half, 1, 2 * half + 1, colour);
    }
}

void draw_background(IRenderer& r, const assets::AssetArchive& archive) {
    const auto* anim = archive.find_animation("sm_screen");
    if (anim != nullptr && !anim->subitems.empty()) {
        const auto& frames = anim->subitems[0].frames;
        for (size_t i = frames.size(); i-- > 0;) r.draw_sprite(frames[i].sprite_index, frames[i].dx, frames[i].dy);
    } else {
        r.fill_rect(0, 0, StartMenu::kWidth, StartMenu::kHeight, kOrange);
    }
}

// The ant of a seat: the animation of the setup screen's portrait (agst301, the colour of the seat), its feet at the bottom of the box
void draw_portrait(IRenderer& r, const assets::AssetArchive& archive, const MenuElement& e, uint32_t now_ms) {
    const auto* anim = archive.find_animation("agst301");
    if (anim == nullptr || anim->subitems.empty()) return;
    const size_t frame = Renderer::get_anim_subitem_by_time(*anim, now_ms);
    const auto& parts = anim->subitems[frame].frames;
    const int32_t ox = e.rect.x + e.rect.w / 2;
    const int32_t oy = e.rect.y + e.rect.h - 6;
    r.set_hud_team(e.team);
    for (size_t k = parts.size(); k-- > 0;) r.draw_sprite(parts[k].sprite_index, ox + parts[k].dx, oy + parts[k].dy);
    r.set_hud_team(0);
}

void draw_button(IRenderer& r, const MenuElement& e) {
    const bool raised = !e.pressed;
    const ColorRGBA& face = e.pressed ? kFacePressed : (e.selected ? kFaceHover : kFace);
    draw_plate(r, e.rect, face, raised, e.selected);
    const ColorRGBA text = e.selected ? kWhite : kCream;
    draw_centered_line(r, e.text, e.rect, text, e.font, e.pressed ? 1 : 0);
}

void draw_cycler(IRenderer& r, const MenuElement& e) {
    const ColorRGBA& face = e.pressed ? kFacePressed : (e.selected ? kFaceHover : kFace);
    draw_plate(r, e.rect, face, !e.pressed, e.selected);
    const ColorRGBA text = e.selected ? kWhite : kCream;
    const int32_t cy = e.rect.y + e.rect.h / 2 + (e.pressed ? 1 : 0);
    draw_arrow(r, true, e.rect.x + 18, cy, text);
    draw_arrow(r, false, e.rect.x + e.rect.w - 18, cy, text);
    ButtonRect inner = e.rect;
    inner.x += 28;
    inner.w -= 56;
    draw_centered_line(r, e.value, inner, text, e.font, e.pressed ? 1 : 0);
}

void draw_field(IRenderer& r, const MenuElement& e) {
    draw_inset(r, e.rect, false);
    const FontSize size = e.font;
    const int32_t cell = font_cell_height(size);
    const int32_t x = e.rect.x + 8;
    const int32_t y = e.rect.y + (e.rect.h - cell) / 2;
    const int32_t width = e.rect.w - 16;
    if (e.all_selected) {                                                         // selected text: its width on a face of the button's green
        const std::string shown = fit_text(r, e.text, width - r.get_text_width("_", size), size);
        r.fill_rect(x - 2, e.rect.y + 5, r.get_text_width(shown, size) + 4, e.rect.h - 10, kFace);
    }
    draw_edit_line(r, e.text, x, y, width, e.selected, e.caret && !e.all_selected, e.all_selected ? kWhite : kCream, size);
    if (e.selected) {                                                              // the field that has the focus: a light frame
        r.draw_rect(e.rect.x + 1, e.rect.y + 1, e.rect.w - 2, e.rect.h - 2, kEdgeLight);
    }
}

void draw_notice(IRenderer& r, const MenuElement& e) {
    draw_inset(r, e.rect, false);
    ButtonRect inner{e.rect.x + 10, e.rect.y + 7, e.rect.w - 20, e.rect.h - 14};
    draw_wrapped(r, e.text, inner, box_text_colour(e.tone), e.font, true, true);
}

void draw_code(IRenderer& r, const MenuElement& e) {
    draw_inset(r, e.rect, false);
    FontSize size = FontSize::Px35;
    const int32_t width = e.rect.w - 24;
    if (r.get_text_width(e.text, size) > width) size = FontSize::Px24;
    if (r.get_text_width(e.text, size) > width) size = FontSize::Px18;
    draw_centered_line(r, e.text, ButtonRect{e.rect.x + 4, e.rect.y, e.rect.w - 8, e.rect.h}, kCream, size);
}

void draw_title(IRenderer& r, const MenuElement& e) {
    draw_plate(r, e.rect, kFace, true, false);
    draw_centered_line(r, e.text, e.rect, kCream, e.font);
}

}  // anonymous namespace

void render_start_menu(IRenderer& renderer, const assets::AssetArchive& archive, const StartMenu& menu) {
    if (menu.wide_layout()) draw_wide_background(renderer, archive, PageClay::Tiles);         // the 16:9 menu: the wide clay and frame (its elements are already where the layout puts them)
    else draw_background(renderer, archive);
    for (const MenuElement& e : menu.elements()) {
        switch (e.kind) {
            case MenuKind::Title: draw_title(renderer, e); break;
            case MenuKind::Text: draw_wrapped(renderer, e.text, e.rect, ink_colour(e.tone), e.font, e.centered); break;
            case MenuKind::Notice: draw_notice(renderer, e); break;
            case MenuKind::Code: draw_code(renderer, e); break;
            case MenuKind::Button: draw_button(renderer, e); break;
            case MenuKind::Cycler: draw_cycler(renderer, e); break;
            case MenuKind::Field: draw_field(renderer, e); break;
            case MenuKind::Portrait: draw_portrait(renderer, archive, e, menu.now_ms()); break;
        }
    }
}

}  // namespace ants::app
