// The windows of the match in the new look (hud.hpp, window_art.hpp): the quit and alliance questions, "Get ready to play!", the quick help, the options window's veil, the countdown card, and the
// Fullscreen button. Each draws the HUD art's pieces when the set has every piece of the window and fits the places of the original's, and the original's own pictures otherwise (window_pieces).
#include <algorithm>
#include <string>

#include "ants_app/hud.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/text_layout.hpp"
#include "ants_app/ui_anim.hpp"
#include "hud_internal.hpp"

namespace ants::app {

namespace {

/// One sprite of the set at a place (its own size, all of it)
void put_sprite(IRenderer& renderer, const hud_art::Art& art, const char* name, int32_t x, int32_t y) {
    const int32_t id = art.find_sprite(name);
    const hud_art::Sprite* s = art.sprite_at(id);
    if (s == nullptr) return;
    hud_art::draw_all(renderer, art, {hud_art::Draw{id, LayoutRect{x, y, s->w, s->h}, LayoutRect{0, 0, s->w, s->h}}});
}

}  // namespace

const window_art::Pieces& HUD::window_pieces(const assets::AssetArchive& assets) {
    const uint64_t generation = art_ != nullptr ? art_->generation() : 0;
    if (!pieces_known_ || pieces_archive_ != &assets || pieces_generation_ != generation) {
        pieces_ = art_ != nullptr ? window_art::check(*art_, assets) : window_art::Pieces{};
        if (pieces_.quick_help) {                         // the new quick help keeps pictures of the original's: without them it is the original's
            kept_pictures_built_ = true;
            if (!kept_pictures_.build(assets)) pieces_.quick_help = false;
        }
        pieces_archive_ = &assets;
        pieces_generation_ = generation;
        pieces_known_ = true;
    }
    return pieces_;
}

void HUD::draw_dialog_frame(IRenderer& renderer, const assets::AssetArchive& assets) {
    if (art_ != nullptr && window_pieces(assets).dialogs) {
        hud_art::draw_all(renderer, *art_, hud_art::panel_draws(*art_, window_art::kDialogRect));
        return;
    }
    hud_detail::draw_std_dialog(renderer, assets);
}

void HUD::draw_dialog_button(IRenderer& renderer, const assets::AssetArchive& assets, const char* name, int32_t x, int32_t y) {
    if (art_ != nullptr && window_pieces(assets).dialogs && hud_art::draw_animation(renderer, *art_, name, 0, layout_)) return;
    draw_animation_frame0(renderer, assets, name, x, y);
}

void HUD::draw_dialog_sentence(IRenderer& renderer, const assets::AssetArchive& assets, const std::string& text, const window_art::SentenceStyle& style, int32_t original_x, int32_t original_y,
                               int32_t original_w, FontSize original_size) {
    if (art_ != nullptr && window_pieces(assets).dialogs && renderer.dialog_text_ready()) {
        window_art::draw_sentence(renderer, text, style, window_art::kDialogRect.x + window_art::kDialogRect.w / 2, window_art::kDialogRect.y);
        return;
    }
    draw_label(renderer, text, original_x, original_y, original_w, window_art::kInk, original_size, true);
}

void HUD::render_quick_help_new(IRenderer& renderer, const assets::AssetArchive& assets) {
    (void)assets;
    hud_art::draw_all(renderer, *art_, hud_art::panel_draws(*art_, window_art::kQuickHelpRect));
    hud_art::draw_animation(renderer, *art_, "qh_window", 0, layout_);
    kept_pictures_.draw(renderer);
    hud_art::draw_animation(renderer, *art_, quick_help_return_.pressed() ? "qh_return3" : (quick_help_return_.hovered() ? "qh_return2" : "qh_return1"), 0, layout_);
}

// The dim behind the options window of the new look: a dark see-through fill over everything of the picture that the card does not cover, where the original lays its checker (render_options_dim). The
// card is the window's own rectangle moved to where the picture puts it; the four bands and the card cover the picture once. The window is modal as it always was: nothing under the veil takes a click.
void HUD::render_veil(IRenderer& renderer) {
    const LayoutPoint o = layout_.options_offset();
    for (const LayoutRect& band : window_art::veil_bands(LayoutRect{0, 0, layout_.width, layout_.height}, window_art::kOptionsRect.moved(o.x, o.y))) {
        if (band.w > 0 && band.h > 0) renderer.fill_rect(band.x, band.y, band.w, band.h, window_art::kVeil);
    }
}

// The countdown card in the new look: the panel, the title and the count's box from the set, the seats' rows and their color dots, and the card's words in the windows' face; the count, the names and the
// words under the count are the game's own text, in its own place as before (render_match_countdown)
void HUD::render_match_countdown_new(IRenderer& renderer, const assets::AssetArchive& assets) {
    using assets::ColorRGBA;
    (void)assets;
    const hud_art::Art& art = *art_;
    const CountdownCardLayout l = countdown_card_layout(layout_.view());
    const LayoutRect& c = l.card;
    const ColorRGBA cream{244, 236, 226, 255};
    const ColorRGBA gold{246, 208, 88, 255};
    const auto centred = [&](const std::string& text, const LayoutRect& box, int32_t y, FontSize size, ColorRGBA color) {
        const int32_t w = renderer.get_text_width(text, size);
        renderer.draw_text(text, box.x + (w < box.w ? (box.w - w) / 2 : 0), y, color, size);
    };
    const auto words = [&](const std::string& text, int32_t em_tenths, int32_t baseline, FontSize fallback, int32_t fallback_y) {   // a line of the card's own words, centred on the card
        if (renderer.dialog_text_ready()) renderer.draw_dialog_text(text, c.x + (c.w - renderer.dialog_text_width(text, em_tenths)) / 2, c.y + baseline, window_art::kInk, em_tenths);
        else centred(text, c, fallback_y, fallback, window_art::kInk);
    };

    hud_art::draw_all(renderer, art, hud_art::panel_draws(art, c));
    hud_art::draw_all(renderer, art, hud_art::animation_draws_at(art, "cd_card", 0, c.x, c.y));

    static const char* const kTeamNames[4] = {"Green", "Red", "Blue", "Black"};
    words(std::string("You are the ") + kTeamNames[local_player_id_ % 4] + " Ants.", 150, 68, FontSize::Px20, l.subtitle_y);

    // the count: whole seconds rounded up, in the box; the words under it have the HUD's dark edge, as the labels of its buttons do
    const int32_t shown = countdown_seconds_shown(match_countdown_left_ms_);
    const LayoutRect& b = l.count_box;
    centred(std::to_string(shown), b, l.digits_y, FontSize::Px96, gold);
    const std::string caption = countdown_caption(shown == 0);
    const int32_t caption_w = renderer.get_text_width(caption, FontSize::Px18);
    renderer.draw_text(caption, b.x + (caption_w < b.w ? (b.w - caption_w) / 2 : 0) + 1, l.caption_y + 1, ColorRGBA{11, 19, 7, 230}, FontSize::Px18);
    centred(caption, b, l.caption_y, FontSize::Px18, cream);

    // who plays which color: the people are "in" (everybody has loaded), a computer player says so
    static const char* const kDots[4] = {"cd_dot_0", "cd_dot_1", "cd_dot_2", "cd_dot_3"};
    for (size_t seat = 0; seat < 4; ++seat) {
        const CountdownSeat& who = countdown_seats_[seat];
        if (!who.plays) continue;
        const LayoutRect& r = l.seat[seat];
        put_sprite(renderer, art, "cd_seat", r.x, r.y);
        put_sprite(renderer, art, kDots[seat], r.x + 9, r.y + 8);
        std::string name = who.name.empty() ? std::string(kTeamNames[seat]) : who.name;
        if (seat == local_player_id_ && !who.computer) name += " (you)";
        const char* tag = who.computer ? "bot" : "in";
        const int32_t tag_w = renderer.get_text_width(tag, FontSize::Px14);
        renderer.draw_text_squeezed(name, r.x + 28, r.y + (r.h - 18) / 2, cream, FontSize::Px18, r.w - 28 - tag_w - 12);
        renderer.draw_text(tag, r.right() - tag_w - 9, r.y + (r.h - 14) / 2, who.computer ? ColorRGBA{168, 194, 184, 255} : ColorRGBA{143, 224, 178, 255}, FontSize::Px14);
    }

    size_t people_in = 0;
    for (const CountdownSeat& who : countdown_seats_) people_in += who.plays && !who.computer ? 1u : 0u;
    words(people_in <= 1 ? "Get ready. Fullscreen is one press away." : (options_.state().chat ? "Everybody is in. Say hello in the chat." : "Everybody is in. The match starts soon."), 135, 300, FontSize::Px18, l.hint_y);

#if !defined(__EMSCRIPTEN__)
    // the native game draws its own Fullscreen button (the browser's page lays a real one over this place: a request for fullscreen needs the click of a real button)
    put_sprite(renderer, art, countdown_button_pressed_ ? "dlg_btn_fullscreen_down" : "dlg_btn_fullscreen_up", l.button.x, l.button.y);
#endif
}

// ---- the Fullscreen button and the corner of the top bar ----------------------------------------------------------------------------------------------------------

bool HUD::fullscreen_button_active() const noexcept {
    return fullscreen_button_.w > 0 && !is_modal_open();
}

// The Classic look's top bar has the three buttons painted in: where they stood the plate is laid again (from the plate's own end columns), and draw_top_button draws them 8 pixels further left
void HUD::draw_top_buttons_corner(IRenderer& renderer, const assets::AssetArchive& assets) {
    if (art_ != nullptr || !top_buttons_moved()) return;
    if (!classic_corner_built_) {
        classic_corner_built_ = true;
        classic_corner_.build(assets);
    }
    if (classic_corner_.ready()) renderer.draw_rgba_blended(layout_.right(window_art::ClassicCorner::kPlateLeft), 7, classic_corner_.plate_w(), 24, classic_corner_.plate().data());
}

void HUD::draw_fullscreen_button(IRenderer& renderer, const assets::AssetArchive& assets) {
    if (fullscreen_button_.w <= 0) return;
    const int state = fullscreen_button_.is_pressed ? 2 : (fullscreen_button_.is_hovered ? 1 : 0);
    if (art_ != nullptr) {
        static const char* const kEnter[3] = {"button_fullscreen_up", "button_fullscreen_hover", "button_fullscreen_down"};
        static const char* const kLeave[3] = {"button_fullscreen_leave_up", "button_fullscreen_leave_hover", "button_fullscreen_leave_down"};
        put_sprite(renderer, *art_, (fullscreen_on_ ? kLeave : kEnter)[state], fullscreen_button_.x, fullscreen_button_.y);
        return;
    }
    if (!classic_corner_built_) {
        classic_corner_built_ = true;
        classic_corner_.build(assets);
    }
    if (classic_corner_.ready()) renderer.draw_rgba_blended(fullscreen_button_.x, fullscreen_button_.y, window_art::ClassicCorner::kButtonW, window_art::ClassicCorner::kButtonH, classic_corner_.button(state, fullscreen_on_).data());
}

}  // namespace ants::app
