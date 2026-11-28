#pragma once

// The countdown card of a match of a room with two or more people (protocol 21, docs/MATCH_COUNTDOWN.md, docs/VIEW_AND_HUD.md "The countdown card"): the "Get ready to play!" dialog of every match,
// made bigger while the host holds the first turn for 30 s. It lies in the middle of the map view and shows the count of the seconds, the colors with who plays them and how good each connection is, a line
// that says the chat is open, and the place of the Fullscreen button; the chat box, the input line and the Send buttons are the HUD's own and stay where they are.
//
// Two looks. The NEW one (the HUD art's windows) is a low, wide card of its own numbers, 480 x 236 UNITS, drawn at a scale that makes a unit a whole number of device pixels (countdown_card_scale: 2 at
// 720p, 3 at 1080p), with the seconds in a well at the left and the players in one list at the right. The CLASSIC one is the original's: clay orange in its thin green frame, 400 x 346 canvas pixels, the
// black boxes of the setup screens, drawn in the picture's own pixels whatever the picture's shape.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

#include "ants_app/display_name.hpp"
#include "ants_app/screen_layout.hpp"
#include "ants_assets/chd_parser.hpp"
#include "ants_net/latency.hpp"
#include "ants_net/p2p_transport.hpp"

namespace ants::app {

/// The card while the player looks around the map: it turns see-through when the view moves and is solid again a moment after it stops. How much of itself each part shows (of 255) at the most
/// see-through: the panel and its frame (the new look: the map shows through), the wells with their writing, the title and the button (they must stay readable over any ground), and the whole of the
/// classic card. How long after the view stopped it is solid again and how fast it goes either way (the steps are the HUD's 50 ms).
inline constexpr int32_t kCardBackSeeThrough = 87;       // 34 %
inline constexpr int32_t kCardFrontSeeThrough = 219;     // 86 %
inline constexpr int32_t kClassicCardSeeThrough = 140;   // 55 %
inline constexpr uint32_t kCardRestSteps = 20;           // 1 s
inline constexpr uint32_t kCardFadeOutSteps = 2;         // to see-through: 100 ms
inline constexpr uint32_t kCardFadeInSteps = 6;          // back to solid: 300 ms

// ---- the classic card ----------------------------------------------------------------------------------------------------------------------------------------------

inline constexpr int32_t kClassicCardW = 400;
inline constexpr int32_t kClassicCardH = 346;

struct ClassicCountdownLayout {
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

/// The classic card in the middle of `view` (the map view); a view that is smaller than the card puts it at the view's top left
constexpr ClassicCountdownLayout classic_countdown_layout(const LayoutRect& view) noexcept {
    ClassicCountdownLayout l;
    const int32_t x = view.x + (view.w > kClassicCardW ? (view.w - kClassicCardW) / 2 : 0);
    const int32_t y = view.y + (view.h > kClassicCardH ? (view.h - kClassicCardH) / 2 : 0);
    l.card = LayoutRect{x, y, kClassicCardW, kClassicCardH};
    l.title_y = y + 12;
    l.subtitle_y = y + 50;
    l.count_box = LayoutRect{x + (kClassicCardW - 170) / 2, y + 80, 170, 122};
    l.digits_y = l.count_box.y + 2;
    l.caption_y = l.count_box.y + 100;
    for (int32_t i = 0; i < 4; ++i) {
        l.seat[static_cast<size_t>(i)] = LayoutRect{x + 16 + (i % 2) * 188, y + 212 + (i / 2) * 34, 180, 28};
    }
    l.hint_y = y + 282;
    l.button = LayoutRect{x + (kClassicCardW - 150) / 2, y + 306, 150, 32};
    return l;
}

// ---- the new card ------------------------------------------------------------------------------------------------------------------------------------------------

/// The new card's own numbers, in units (the HUD art's cd_card, cd_row, cd_dot_N and Fullscreen sprites are drawn in them; tools/hud_art/dialog_windows.py has the same)
inline constexpr int32_t kCardW = 480;
inline constexpr int32_t kCardH = 236;
inline constexpr LayoutRect kCardWell{24, 74, 140, 106};            // the seconds
inline constexpr int32_t kCardRowX = 188;                           // the players' list: rows 268 x 21, 9 apart, centered on the well
inline constexpr int32_t kCardRowW = 268;
inline constexpr int32_t kCardRowH = 21;
inline constexpr int32_t kCardRowGap = 9;
inline constexpr LayoutRect kCardButton{364, 196, 94, 22};          // the Fullscreen button's face

/// The card's scale: how many canvas pixels one unit is (`f`) and the HUD art's sheet (1, 2 or 3) that draws a unit exactly. A unit is the largest whole number `k` of device pixels such that the card has
/// 360 units on the shorter side of the picture (2 at 1280 x 720, 3 at 1920 x 1080 and on a phone held sideways) and still lies inside `view` (a smaller one takes fewer, down to one); `output` is
/// how many device pixels a canvas pixel is. A view that is too small even for one device pixel to the unit shrinks the card to the view.
struct CountdownCardScale {
    double f{1.0};
    int32_t sheet{1};
};

inline CountdownCardScale countdown_card_scale(const LayoutRect& view, int32_t canvas_h, double output) noexcept {
    const double s = output > 0.0 ? output : 1.0;
    int32_t k = std::max<int32_t>(1, static_cast<int32_t>(std::floor(static_cast<double>(canvas_h) * s / 360.0 + 1e-6)));
    const auto fits = [&](int32_t n) { return static_cast<double>(kCardW * n) <= static_cast<double>(view.w) * s + 1e-6 && static_cast<double>(kCardH * n) <= static_cast<double>(view.h) * s + 1e-6; };
    while (k > 1 && !fits(k)) --k;
    CountdownCardScale out;
    out.f = static_cast<double>(k) / s;
    if (!fits(k)) out.f = std::max(0.25, std::min(static_cast<double>(view.w) / kCardW, static_cast<double>(view.h) / kCardH));
    out.sheet = std::clamp<int32_t>(static_cast<int32_t>(std::lround(out.f * s)), 1, 3);
    return out;
}

/// The new card in the middle of `view`, a little above the middle (16 units; 8 units from the top at the least), at `scale`: every rectangle and anchor in canvas pixels
struct CountdownCard {
    CountdownCardScale scale;
    LayoutRect card;
    std::array<LayoutRect, 4> row{};                   // the players' rows, from the top, as many as `rows`
    std::array<int32_t, 4> row_y{};                    // their tops in units (the art's cd_row is drawn at (kCardRowX, row_y))
    size_t rows{0};
    std::array<LayoutRect, 4> dot{};                   // their color squares (11 x 11 units)
    LayoutRect well;                                   // the seconds' well's ground
    LayoutRect button;                                 // the Fullscreen button's face (the page's own button is laid over it on the web)
    LayoutRect content;                                // everything that is not the panel's: from the top to the lowest of the rows and the well, for the page's button to keep clear of

    int32_t x(double units) const noexcept { return card.x + static_cast<int32_t>(std::lround(units * scale.f)); }
    int32_t y(double units) const noexcept { return card.y + static_cast<int32_t>(std::lround(units * scale.f)); }
    /// A rectangle given in units, in canvas pixels (the edges are rounded, so neighbors share an edge)
    LayoutRect rect(int32_t ux, int32_t uy, int32_t uw, int32_t uh) const noexcept {
        const int32_t x0 = x(ux), y0 = y(uy), x1 = x(ux + uw), y1 = y(uy + uh);
        return LayoutRect{x0, y0, x1 - x0, y1 - y0};
    }
    /// A size in units as the em of a line of text, in tenths of a canvas pixel
    int32_t em_tenths(double units) const noexcept { return static_cast<int32_t>(std::lround(units * scale.f * 10.0)); }
};

/// The card for `rows` players (0 to 4) in `view` at `scale`
inline CountdownCard countdown_card(const LayoutRect& view, CountdownCardScale scale, size_t rows) noexcept {
    CountdownCard c;
    c.scale = scale;
    c.rows = std::min<size_t>(rows, 4);
    const int32_t w = static_cast<int32_t>(std::lround(kCardW * scale.f));
    const int32_t h = static_cast<int32_t>(std::lround(kCardH * scale.f));
    const int32_t above = static_cast<int32_t>(std::lround(16.0 * scale.f));
    const int32_t top_min = static_cast<int32_t>(std::lround(8.0 * scale.f));
    int32_t dy = (view.h - h) / 2 - above;
    dy = std::max(dy, std::min(top_min, view.h - h));
    dy = std::max(0, dy);
    c.card = LayoutRect{view.x + std::max(0, (view.w - w) / 2), view.y + dy, w, h};
    c.well = c.rect(kCardWell.x, kCardWell.y, kCardWell.w, kCardWell.h);
    const int32_t n = static_cast<int32_t>(c.rows);
    const int32_t total = n * kCardRowH + std::max(0, n - 1) * kCardRowGap;
    const int32_t y0 = kCardWell.y + (kCardWell.h - total + 1) / 2;
    int32_t lowest = kCardWell.y + kCardWell.h;
    for (int32_t i = 0; i < n; ++i) {
        const int32_t ry = y0 + i * (kCardRowH + kCardRowGap);
        c.row_y[static_cast<size_t>(i)] = ry;
        c.row[static_cast<size_t>(i)] = c.rect(kCardRowX, ry, kCardRowW, kCardRowH);
        c.dot[static_cast<size_t>(i)] = c.rect(kCardRowX + 7, ry + 5, 11, 11);
        lowest = std::max(lowest, ry + kCardRowH);
    }
    c.button = c.rect(kCardButton.x, kCardButton.y, kCardButton.w, kCardButton.h);
    c.content = LayoutRect{c.card.x, c.card.y, c.card.w, c.y(lowest) - c.card.y};
    return c;
}

// ---- what the card says ------------------------------------------------------------------------------------------------------------------------------------------

/// What the card shows of one color
struct CountdownSeat {
    bool plays{false};                                 // the color is in the match
    bool computer{false};                              // a computer player (its name says so: "Pip (bot, Hard)")
    std::string name;
    net::PeerLink link{net::PeerLink::None};           // how this machine is connected to that person's page once it is (protocol 26): the mark "relay" or "direct" stands in the row; never a word about an address
    int32_t ping_ms{-1};                               // the round trip that the row shows (ants_net/latency.hpp: your own to the server, another person's over the channel); -1: no number yet
    bool shows_ping{false};                            // a game over the network: a person's row says a number or "-- ms" (a game on this computer and a computer player show none)
};

/// The small word at the right end of a color's row: "bot" for a computer player, else "in" (the person has loaded), or, once this machine has a channel to that person, "relay" or "direct"
inline const char* countdown_tag(const CountdownSeat& who) noexcept {
    if (who.computer) return "bot";
    if (who.link == net::PeerLink::Direct) return "direct";
    if (who.link == net::PeerLink::Relay) return "relay";
    return "in";
}

/// The words of one row: the name (the player's own told: "(you)"), the small word (a computer player's level for its tag: "Pip (bot, Hard)" is the name "Pip" and the tag "bot, Hard"; your own row
/// says "server": the number is your round trip to it; another person's says "direct", "relay" or "in") and the number ("38 ms", "-- ms" while a person has none, nothing for a computer player or a game
/// on this computer)
struct CountdownRowWords {
    std::string name;
    std::string tag;
    std::string ping;
};

inline CountdownRowWords countdown_row_words(const CountdownSeat& who, bool mine, const char* color_name) {
    CountdownRowWords w;
    w.name = who.name.empty() ? std::string(color_name) : without_clan_tag(who.name);       // (the clan tag is not shown here: a row has room for the name)
    if (who.computer) {
        const size_t open = w.name.find(" (bot");
        const size_t close = w.name.rfind(')');
        if (open != std::string::npos && close != std::string::npos && close > open + 2 && close + 1 == w.name.size()) {
            w.tag = w.name.substr(open + 2, close - open - 2);
            w.name.resize(open);
        } else {
            w.tag = "bot";
        }
        return w;
    }
    if (mine) {
        w.name += " (you)";
        w.tag = who.shows_ping ? "server" : "in";
    } else {
        w.tag = countdown_tag(who);
    }
    if (who.shows_ping) w.ping = who.ping_ms >= 0 ? std::to_string(std::min<int32_t>(who.ping_ms, 999)) + " ms" : "-- ms";
    return w;
}

/// The color of a number by how good the round trip is (net::ping_quality): green under 80 ms, gold under 150, red from there, gray for no number (the card's rows and the list of connections in a match)
inline assets::ColorRGBA ping_color(int32_t ms) noexcept {
    switch (net::ping_quality(ms)) {
        case net::PingQuality::Good: return assets::ColorRGBA{154, 240, 180, 255};
        case net::PingQuality::Fair: return assets::ColorRGBA{246, 208, 88, 255};
        case net::PingQuality::Poor: return assets::ColorRGBA{255, 149, 133, 255};
        case net::PingQuality::None: break;
    }
    return assets::ColorRGBA{168, 182, 176, 255};
}

/// The words of the count under its digits
inline const char* countdown_caption(bool counted_down) noexcept { return counted_down ? "starting..." : "seconds to the start"; }

/// The count that the card shows for `ms_left` milliseconds still to go: whole seconds rounded up (30 s down to 1), 0 at the end
constexpr int32_t countdown_seconds_shown(int64_t ms_left) noexcept { return ms_left <= 0 ? 0 : static_cast<int32_t>((ms_left + 999) / 1000); }

}  // namespace ants::app
