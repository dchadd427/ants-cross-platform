#pragma once

// The countdown card of a match of a room with two or more people (protocol 21, docs/MATCH_COUNTDOWN.md, docs/VIEW_AND_HUD.md "The countdown"): the "Get ready to play!" dialog of every match,
// made bigger while the host holds the first turn for 30 s: the count in a big black box, the four colors with who plays them, a line that says the chat is open, and the place of the Fullscreen
// button. It lies in the middle of the map view (the picture the owner approved: clay orange, the thin green frame, the black boxes of the setup screens), in the picture's own pixels, whatever the
// picture's shape; the chat box, the input line and the Send buttons are the HUD's own and stay where they are.

#include <array>
#include <cstdint>
#include <string>

#include "ants_app/screen_layout.hpp"

namespace ants::app {

inline constexpr int32_t kCountdownCardW = 400;
inline constexpr int32_t kCountdownCardH = 346;

struct CountdownCardLayout {
    LayoutRect card;                                   // the whole card, frame included (4 px of green inside a 1 px black line)
    int32_t title_y{0};                                // "Get ready to play!" (35 px letters, centred in the card)
    int32_t subtitle_y{0};                             // "You are the Blue Ants." (20 px)
    LayoutRect count_box;                              // the black box with the count (96 px digits) and its caption
    int32_t digits_y{0};
    int32_t caption_y{0};
    std::array<LayoutRect, 4> seat;                    // the colors in the order of the seats (green, red, blue, black): two rows of two
    int32_t hint_y{0};
    LayoutRect button;                                 // the Fullscreen button (the page's own on the web: it is laid over this place; a drawn one in the native game)
};

/// The card in the middle of `view` (the map view); a view that is smaller than the card puts it at the view's top left
constexpr CountdownCardLayout countdown_card_layout(const LayoutRect& view) noexcept {
    CountdownCardLayout l;
    const int32_t x = view.x + (view.w > kCountdownCardW ? (view.w - kCountdownCardW) / 2 : 0);
    const int32_t y = view.y + (view.h > kCountdownCardH ? (view.h - kCountdownCardH) / 2 : 0);
    l.card = LayoutRect{x, y, kCountdownCardW, kCountdownCardH};
    l.title_y = y + 12;
    l.subtitle_y = y + 50;
    l.count_box = LayoutRect{x + (kCountdownCardW - 170) / 2, y + 80, 170, 122};
    l.digits_y = l.count_box.y + 2;
    l.caption_y = l.count_box.y + 100;
    for (int32_t i = 0; i < 4; ++i) {
        l.seat[static_cast<size_t>(i)] = LayoutRect{x + 16 + (i % 2) * 188, y + 212 + (i / 2) * 34, 180, 28};
    }
    l.hint_y = y + 282;
    l.button = LayoutRect{x + (kCountdownCardW - 150) / 2, y + 306, 150, 32};
    return l;
}

/// What the card shows of one color
struct CountdownSeat {
    bool plays{false};                                 // the color is in the match
    bool computer{false};                              // a computer player (its name says so: "Pip (bot, Hard)")
    std::string name;
};

/// The words of the count under its digits
inline const char* countdown_caption(bool counted_down) noexcept { return counted_down ? "starting..." : "seconds to the start"; }

/// The count that the card shows for `ms_left` milliseconds still to go: whole seconds rounded up (30 s down to 1), 0 at the end
constexpr int32_t countdown_seconds_shown(int64_t ms_left) noexcept { return ms_left <= 0 ? 0 : static_cast<int32_t>((ms_left + 999) / 1000); }

}  // namespace ants::app
