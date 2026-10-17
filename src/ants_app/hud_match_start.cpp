// The dialog of a match's start on the match screen (hud.hpp): the original's "Get ready to play!" dialog (Ants.exe 0x1017127) and, for a match of a room with two or more people (network protocol 21),
// the COUNTDOWN CARD (countdown_card.hpp, docs/VIEW_AND_HUD.md "The countdown card") that takes its place while the host holds the first turn: the card, the keys and the clicks that it lets through
// (the chat's, Quit and Fullscreen), everything else stays out of the match.
#include "ants_app/hud.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/text_layout.hpp"

#include <string>

namespace ants::app {

void HUD::render_match_start_modal(IRenderer& renderer, const assets::AssetArchive& assets) {
    if (match_countdown_) {                       // protocol 21: the card, in the picture's own pixels in the middle of the map view (the caller's offset is the original dialog's)
        renderer.set_origin(0, 0);
        render_match_countdown(renderer, assets);
        return;
    }
    using assets::ColorRGBA;

    // Start modal (Ants.exe 0x1017127): std_dialg at origin (100,100), one wrapped centred label (string 105) in the
    // rect (130,110) 240x160, footer (string 104) in (130,290) 240x20, animated worker portrait at (245,250).
    const int32_t mx = 100;
    const int32_t my = 100;

    const auto* seq = assets.find_animation("std_dialg");
    if (seq && !seq->subitems.empty()) {
        const auto& frames = seq->subitems[0].frames;
        for (size_t i = frames.size(); i-- > 0; ) {
            renderer.draw_sprite(frames[i].sprite_index, mx + frames[i].dx, my + frames[i].dy);
        }
    } else {
        renderer.fill_rect(mx, my, 320, 224, ColorRGBA{219, 75, 19, 255});
        renderer.draw_rect(mx, my, 320, 224, ColorRGBA{36, 82, 77, 255});
    }

    // Dark slate text color #1F1733 (COLORREF 0x33171F)
    const ColorRGBA text_color{31, 23, 51, 255};

    // Label: "Get ready to play!  You are the <Color> Ants." (string 105): the original's wrapped, centred label of 35 px letters in the box
    // (30, 10) 240 x 160 of the dialog (FUN_01017127: FUN_0102b0b5 wraps it, DrawTextA centres every line, the lines are one cell height apart)
    static const char* TEAM_NAMES[4] = {"Green", "Red", "Blue", "Black"};
    const std::string label = std::string("Get ready to play!  You are the ") + TEAM_NAMES[local_player_id_ % 4] + " Ants.";
    draw_label(renderer, label, mx + 30, my + 10, 240, text_color, FontSize::Px35, true);

    // Animated portrait of the local color's worker ant (agst301: 12 frames, 1650 ms loop), anchor (245,250)
    renderer.set_hud_team(local_player_id_);
    if (const auto* ant = assets.find_animation("agst301")) {
        if (!ant->subitems.empty()) {
            const uint32_t ms = match_start_modal_ticks_ * 50u;
            const size_t frame = Renderer::get_anim_subitem_by_time(*ant, ms);
            const auto& parts = ant->subitems[frame].frames;
            for (size_t k = parts.size(); k-- > 0; ) {
                renderer.draw_sprite(parts[k].sprite_index, 245 + parts[k].dx, 250 + parts[k].dy);
            }
        }
    }
    renderer.set_hud_team(0);

    // Footer: "Waiting for others..." (string 104): a centred label of 24 px letters in the box (30, 190) 240 x 20
    draw_label(renderer, "Waiting for others...", mx + 30, my + 190, 240, text_color, FontSize::Px24, true);
}

// The keys of the chat while the countdown card is up (protocol 21): the chat's own keys and nothing of the match. Backspace edits the line, Enter sends it (to the team when the player has an ally, as
// in the match), F9 - F12 send the quick chat texts, Ctrl+Q asks to quit as it does in the match; every other key is swallowed, as the dialog of every match swallows them. The printable keys are
// swallowed too: the characters of a line come as text input (handle_text_input: SDL_TEXTINPUT, which the application hands over whatever dialog is up, and a phone's keyboard through the page), and
// a key event beside it would type every letter twice (the application does not hand a printable key to the HUD when no dialog is up; with the card up it does).
bool HUD::handle_countdown_key(int32_t key, uint16_t mod, bool repeat) {
    const bool ctrl = (mod & KMOD_CTRL) != 0 || (mod & KMOD_GUI) != 0;
    if (options_.state().chat && !ctrl && key == SDLK_BACKSPACE) {
        if (!chat_input_.empty()) chat_input_.pop_back();
        return true;
    }
    switch (key) {
        case SDLK_F9: case SDLK_F10: case SDLK_F11: case SDLK_F12:
            if (!repeat && options_.state().chat) trigger_quick_chat(static_cast<size_t>(key - SDLK_F9));
            return true;
        case SDLK_RETURN: case SDLK_KP_ENTER:
            send_chat_message();
            return true;
        case 'q': case 'Q':
            if (ctrl) open_quit_dialog();
            return true;
        default:
            return true;
    }
}

// The countdown card (protocol 21, countdown_card.hpp): the "Get ready to play!" dialog of every match, bigger while the host holds the first turn for 30 s. The colors are the original's: the clay
// orange of the dialog, the thin green frame, the dark violet letters (#1F1733), and the black boxes of the setup screens for the count and the colors.
void HUD::render_match_countdown(IRenderer& renderer, const assets::AssetArchive& assets) {
    using assets::ColorRGBA;
    (void)assets;
    const CountdownCardLayout l = countdown_card_layout(layout_.view());
    const LayoutRect& c = l.card;
    const ColorRGBA clay{219, 75, 19, 255};
    const ColorRGBA ink{31, 23, 51, 255};                // dark slate (COLORREF 0x33171F), the dialog's own letters
    const ColorRGBA cream{244, 236, 226, 255};
    const ColorRGBA gold{246, 208, 88, 255};
    const ColorRGBA black{5, 10, 8, 255};
    const ColorRGBA frame{43, 95, 67, 255};
    const ColorRGBA inset{7, 11, 15, 255};
    const auto centred = [&](const std::string& text, const LayoutRect& box, int32_t y, FontSize size, ColorRGBA color) {
        const int32_t w = renderer.get_text_width(text, size);
        renderer.draw_text(text, box.x + (w < box.w ? (box.w - w) / 2 : 0), y, color, size);
    };

    renderer.fill_rect(c.x + 4, c.y + 6, c.w, c.h, ColorRGBA{157, 13, 23, 230});          // the red shadow under every picture of the front page
    renderer.fill_rect(c.x - 1, c.y - 1, c.w + 2, c.h + 2, black);
    renderer.fill_rect(c.x, c.y, c.w, c.h, frame);
    renderer.fill_rect(c.x + 4, c.y + 4, c.w - 8, c.h - 8, clay);

    static const char* const kTeamNames[4] = {"Green", "Red", "Blue", "Black"};
    centred("Get ready to play!", c, l.title_y, FontSize::Px35, ink);
    centred(std::string("You are the ") + kTeamNames[local_player_id_ % 4] + " Ants.", c, l.subtitle_y, FontSize::Px20, ink);

    // the count: whole seconds rounded up, in a black box with a green frame (the setup screens' boxes)
    const int32_t shown = countdown_seconds_shown(match_countdown_left_ms_);
    const LayoutRect& b = l.count_box;
    renderer.fill_rect(b.x - 2, b.y - 2, b.w + 4, b.h + 4, black);
    renderer.fill_rect(b.x, b.y, b.w, b.h, ColorRGBA{59, 151, 111, 255});
    renderer.fill_rect(b.x + 3, b.y + 3, b.w - 6, b.h - 6, inset);
    centred(std::to_string(shown), b, l.digits_y, FontSize::Px96, gold);
    centred(countdown_caption(shown == 0), b, l.caption_y, FontSize::Px18, cream);

    // who plays which color: the people are "in" (everybody has loaded), a computer player says so
    static const ColorRGBA kDots[4] = {{63, 174, 106, 255}, {209, 75, 85, 255}, {92, 111, 224, 255}, {110, 110, 118, 255}};
    for (size_t seat = 0; seat < 4; ++seat) {
        const CountdownSeat& who = countdown_seats_[seat];
        if (!who.plays) continue;
        const LayoutRect& r = l.seat[seat];
        renderer.fill_rect(r.x, r.y, r.w, r.h, ColorRGBA{5, 10, 8, 235});
        renderer.fill_rect(r.x + 7, r.y + (r.h - 12) / 2, 12, 12, black);
        renderer.fill_rect(r.x + 8, r.y + (r.h - 12) / 2 + 1, 10, 10, kDots[seat]);
        std::string name = who.name.empty() ? std::string(kTeamNames[seat]) : who.name;
        if (seat == local_player_id_ && !who.computer) name += " (you)";
        const char* tag = who.computer ? "bot" : "in";
        const int32_t tag_w = renderer.get_text_width(tag, FontSize::Px14);
        renderer.draw_text_squeezed(name, r.x + 26, r.y + (r.h - 18) / 2, cream, FontSize::Px18, r.w - 26 - tag_w - 14);
        renderer.draw_text(tag, r.right() - tag_w - 8, r.y + (r.h - 14) / 2, who.computer ? ColorRGBA{168, 194, 184, 255} : ColorRGBA{143, 224, 178, 255}, FontSize::Px14);
    }

    centred(options_.state().chat ? "Everybody is in. Say hello in the chat." : "Everybody is in. The match starts soon.", c, l.hint_y, FontSize::Px18, ink);

#if !defined(__EMSCRIPTEN__)
    // the native game draws its own Fullscreen button (the browser's page lays a real one over this place: a request for fullscreen needs the click of a real button)
    const LayoutRect& f = l.button;
    renderer.fill_rect(f.x - 1, f.y - 1, f.w + 2, f.h + 2, black);
    renderer.fill_rect(f.x, f.y, f.w, f.h, countdown_button_pressed_ ? ColorRGBA{23, 56, 47, 255} : ColorRGBA{43, 99, 87, 255});
    centred("Fullscreen", f, f.y + (f.h - 18) / 2 + (countdown_button_pressed_ ? 1 : 0), FontSize::Px18, cream);
#endif
}

// A press while the card is up (protocol 21): the map is out of reach, the chat's buttons, Quit and Fullscreen are not. The press only captures the button; the release acts on it.
bool HUD::handle_countdown_mouse_down(int32_t x, int32_t y, uint8_t button) {
    if (button != SDL_BUTTON_LEFT) return true;
    if (quit_button_.contains(x, y)) {
        quit_button_.is_pressed = true;
        play_sfx(sim::SoundID::ButtonClick);
    } else if (options_.state().chat && send_to_button_.contains(x, y)) {
        send_to_button_.is_pressed = true;
        play_sfx(sim::SoundID::ButtonClick);
    } else if (options_.state().chat && is_on_team_ && team_button_.contains(x, y)) {
        team_button_.is_pressed = true;
        play_sfx(sim::SoundID::ButtonClick);
    } else if (countdown_card_layout(layout_.view()).button.contains(x, y)) {
        countdown_button_pressed_ = true;
        play_sfx(sim::SoundID::ButtonClick);
    }
    return true;
}

// A release while the card is up: what a press captured acts at the release, on the button (`fire_*` are the release's own decisions, handle_mouse_up)
bool HUD::handle_countdown_mouse_up(int32_t x, int32_t y, bool left_release, bool fire_quit, bool fire_all, bool fire_team) {
    const bool fire_fullscreen = left_release && countdown_button_pressed_ && countdown_card_layout(layout_.view()).button.contains(x, y);
    countdown_button_pressed_ = false;
    if (fire_quit) open_quit_dialog();
    else if (fire_all) send_chat(false);
    else if (fire_team) send_chat(true);
    else if (fire_fullscreen && on_fullscreen_) on_fullscreen_();
    return true;
}

}  // namespace ants::app
