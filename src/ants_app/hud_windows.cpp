// The windows of the match in the new look (hud.hpp, window_art.hpp): the quit and alliance questions, "Get ready to play!", the quick help, the options window's veil, the countdown card, and the
// Fullscreen button. Each draws the HUD art's pieces when the set has every piece of the window and fits the places of the original's, and the original's own pictures otherwise (window_pieces).
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "ants_app/hud.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/text_layout.hpp"
#include "ants_app/ui_anim.hpp"
#include "ants_app/unit_pen.hpp"
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

namespace {

/// The pieces of the new card are drawn in the card's own units and put on the canvas at the card's scale (countdown_card.hpp): a sprite's rectangle is mapped edge by edge, so neighbors share an edge
std::vector<hud_art::Draw> on_card(const CountdownCard& card, std::vector<hud_art::Draw> draws) {
    for (hud_art::Draw& d : draws) d.dst = card.rect(d.dst.x, d.dst.y, d.dst.w, d.dst.h);
    return draws;
}

std::vector<hud_art::Draw> sprite_on_card(const hud_art::Art& art, const char* name, int32_t ux, int32_t uy) {
    const int32_t id = art.find_sprite(name);
    const hud_art::Sprite* s = art.sprite_at(id);
    if (s == nullptr) return {};
    return {hud_art::Draw{id, LayoutRect{ux, uy, s->w, s->h}, LayoutRect{0, 0, s->w, s->h}}};
}

assets::ColorRGBA mix(assets::ColorRGBA a, assets::ColorRGBA b, int32_t t255) {
    const auto lerp = [&](uint8_t x, uint8_t y) { return static_cast<uint8_t>((x * (255 - t255) + y * t255 + 127) / 255); };
    return assets::ColorRGBA{lerp(a.r, b.r), lerp(a.g, b.g), lerp(a.b, b.b), 255};
}

}  // namespace

// The countdown card in the new look (countdown_card.hpp: 480 x 236 units at a scale that makes a unit whole device pixels): the panel and its frame from the set, the title and the seconds' well, a well
// for each player with a color square, and the button; the game writes the rest (the seconds, the names, the numbers, the two sentences) in its faces. While the player looks round the map the panel
// and its frame fade to a third (the map shows through) and everything that must stay readable keeps most of itself; the two sentences stand on the panel, so they turn light with a dark edge as it goes.
void HUD::render_match_countdown_new(IRenderer& renderer, const assets::AssetArchive& assets) {
    using assets::ColorRGBA;
    (void)assets;
    const hud_art::Art& art = *art_;
    const CountdownCard card = countdown_card_now();
    const LayoutRect& c = card.card;
    const UnitPen pen(renderer, card.scale.f);
    const ColorRGBA cream{244, 236, 226, 255};
    const ColorRGBA gold{246, 208, 88, 255};
    const ColorRGBA dim{168, 194, 184, 255};
    const int32_t look = card_look_;

    renderer.set_art_scale(card.scale.sheet);
    renderer.set_alpha_scale(255 - look * (255 - kCardBackSeeThrough) / 255);
    hud_art::draw_all(renderer, art, on_card(card, hud_art::panel_draws(art, LayoutRect{0, 0, kCardW, kCardH})));
    renderer.set_alpha_scale(countdown_card_opacity());
    hud_art::draw_all(renderer, art, on_card(card, hud_art::animation_draws_at(art, "cd_card", 0, 0, 0)));

    // the sentences of the card (the windows' face): dark ink on the panel, light with a dark edge as the panel fades
    const auto sentence = [&](const std::string& text, int32_t x, int32_t baseline, double em_units, int align) {
        const int32_t em = card.em_tenths(em_units);
        if (renderer.dialog_text_ready()) {
            const int32_t left = align == 1 ? x - renderer.dialog_text_width(text, em) / 2 : x;
            if (look > 0) renderer.draw_dialog_text(text, left + 1, baseline + 1, ColorRGBA{11, 15, 13, static_cast<uint8_t>(look * 230 / 255)}, em);
            renderer.draw_dialog_text(text, left, baseline, mix(window_art::kInk, cream, look), em);
        } else {
            pen.draw(text, x, baseline, mix(window_art::kInk, cream, look), em_units, align);
        }
    };
    static const char* const kTeamNames[4] = {"Green", "Red", "Blue", "Black"};
    sentence(std::string("You are the ") + kTeamNames[local_player_id_ % 4] + " Ants.", c.x + c.w / 2, card.y(58), 13.0, 1);

    // the count: whole seconds rounded up, in the well; the words under it are small and dim
    const int32_t shown = countdown_seconds_shown(match_countdown_left_ms_);
    const int32_t well_mid = card.x(kCardWell.x + kCardWell.w / 2.0);
    pen.draw(std::to_string(shown), well_mid, card.y(kCardWell.y + 74), gold, 82.0, 1);
    pen.draw(countdown_caption(shown == 0), well_mid, card.y(kCardWell.y + kCardWell.h - 9), dim, 10.5, 1);

    // one well for each color that plays: its square, the name, the small word and the number in the color of its quality
    static const char* const kDots[4] = {"cd_dot_0", "cd_dot_1", "cd_dot_2", "cd_dot_3"};
    size_t line = 0;
    for (size_t seat = 0; seat < 4 && line < card.rows; ++seat) {
        const CountdownSeat& who = countdown_seats_[seat];
        if (!who.plays) continue;
        const int32_t ry = card.row_y[line];
        hud_art::draw_all(renderer, art, on_card(card, hud_art::animation_draws_at(art, "cd_row", 0, kCardRowX, ry)));
        hud_art::draw_all(renderer, art, on_card(card, sprite_on_card(art, kDots[seat], kCardRowX + 7, ry + 5)));
        const CountdownRowWords words = countdown_row_words(who, seat == local_player_id_ && !who.computer, kTeamNames[seat]);
        const int32_t right = card.x(kCardRowX + kCardRowW - 7);
        const int32_t tag_right = card.x(kCardRowX + kCardRowW - 7 - (words.ping.empty() ? 0 : 62));
        pen.draw(words.ping, right, card.y(ry + 15.5), ping_color(who.ping_ms >= 0 ? who.ping_ms : -1), 12.5, 2);
        pen.draw(words.tag, tag_right, card.y(ry + 15), dim, 10.5, 2);
        const int32_t name_left = card.x(kCardRowX + 25);
        const int32_t room = tag_right - pen.width(words.tag, 10.5) - static_cast<int32_t>(std::lround(10.0 * card.scale.f)) - name_left;      // (the name stops 10 units short of the small word)
        pen.draw(pen.fit(words.name, room, 13.5), name_left, card.y(ry + 16), cream, 13.5);
        ++line;
    }

    size_t people_in = 0;
    for (const CountdownSeat& who : countdown_seats_) people_in += who.plays && !who.computer ? 1u : 0u;
    sentence(people_in <= 1 ? "Get ready. Fullscreen is one press away." : (options_.state().chat ? "Everybody is in. Say hello in the chat." : "Everybody is in. The match starts soon."), card.x(24), card.y(211), 12.0, 0);

#if !defined(__EMSCRIPTEN__)
    // the native game draws its own Fullscreen button (the browser's page lays a real one over this place: a request for fullscreen needs the click of a real button)
    hud_art::draw_all(renderer, art, on_card(card, sprite_on_card(art, countdown_button_pressed_ ? "dlg_btn_fullscreen_down" : "dlg_btn_fullscreen_up", kCardButton.x, kCardButton.y)));
#endif
    renderer.set_art_scale(0);
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
