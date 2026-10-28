#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace ants::app::health {

// The ant health marks (docs/VIEW_AND_HUD.md, "Ant health marks"): a small bar over an ant, or the original's own hit point number (white digits of its fixed font at the ant's lower right), drawn from the
// hit points that every client already has. This file is the rule with no window and no SDL (so that a test can run it): which ants wear a mark, the bar's color, and where and how big the bar is on the
// screen. Renderer::draw_health_marks (src/ants_app/renderer_health.cpp) draws the bars, the number is drawn where the original draws it, after its ant. Drawing only: nothing here is read by the
// simulation, a recording or the network.

/// When an ant wears a mark. The values are the menu row's.
enum class When : uint8_t { Hurt = 0, All = 1, Off = 2 };
/// What the mark looks like. The values are the menu row's.
enum class Style : uint8_t { Bar = 0, Number = 1 };

struct Setting {
    When when{When::Hurt};      // hurt ants only, with a bar: the default for everybody
    Style style{Style::Bar};
    friend bool operator==(const Setting& a, const Setting& b) noexcept { return a.when == b.when && a.style == b.style; }
    friend bool operator!=(const Setting& a, const Setting& b) noexcept { return !(a == b); }
};

/// The words of the settings file (`ant_health=hurt|all|off`, `ant_health_style=bar|number`); a parse that fails leaves `out` alone
inline const char* word(When when) noexcept { return when == When::All ? "all" : (when == When::Off ? "off" : "hurt"); }
inline const char* word(Style style) noexcept { return style == Style::Number ? "number" : "bar"; }
inline bool parse(const std::string& text, When& out) noexcept {
    for (const When w : {When::Hurt, When::All, When::Off}) {
        if (text == word(w)) { out = w; return true; }
    }
    return false;
}
inline bool parse(const std::string& text, Style& out) noexcept {
    for (const Style s : {Style::Bar, Style::Number}) {
        if (text == word(s)) { out = s; return true; }
    }
    return false;
}

// ---- the rule -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

/// The health colors of the selection ears: 9 and 10 hit points are green (dogears), 3 to 8 amber (yelears), 1 and 2 red (redears). One home: the ears of a selected ant use these edges too.
constexpr int kGreenFrom = 9;
constexpr int kRedUpTo = 2;

enum class Band : uint8_t { Red, Amber, Green };
constexpr Band band(int hp) noexcept { return hp >= kGreenFrom ? Band::Green : (hp <= kRedUpTo ? Band::Red : Band::Amber); }

struct Color {
    uint8_t r, g, b;
};
constexpr Color color(Band b) noexcept {
    return b == Band::Green ? Color{60, 214, 60} : (b == Band::Red ? Color{236, 44, 44} : Color{255, 196, 24});
}

/// Does the ant wear a mark? Every ant has 10 hit points at most, so "hurt" is "fewer than its own most". A dead ant (no hit points left, still on its way out) and a frozen one (the display loop of the
/// original skips it) wear none. The caller has already left out an ant that fog of war hides: it is not collected at all.
constexpr bool shown(When when, int hp, int max_hp, bool frozen) noexcept {
    if (when == When::Off || frozen || hp <= 0) return false;
    return when == When::All || hp < max_hp;
}

/// Does the ant wear a NUMBER? The same choice of ants, but the number is the original's own (Ctrl+L: after every ant that is drawn, and not a frozen one, its hit points, a dying ant's "0" included),
/// so only Off and a frozen ant leave it out; Hurt ants is the ants that have fewer than their own most.
constexpr bool shown_number(When when, int hp, int max_hp, bool frozen) noexcept {
    if (when == When::Off || frozen) return false;
    return when == When::All || hp < max_hp;
}

// ---- where and how big (screen pixels) --------------------------------------------------------------------------------------------------------------------------------------------------------------------

struct Rect {
    int32_t x, y, w, h;
};
constexpr bool overlaps(const Rect& a, const Rect& b) noexcept { return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h; }

/// The bar stands over the ant's head (the number is at the ant's lower right, renderer_health.cpp): its lower edge is this many world pixels above the ant's position (scaled by the zoom)
constexpr int32_t kHeadAbove = 31;
constexpr int32_t kBarWorldWidth = 22;      // a bar is 22 world pixels wide (scaled by the zoom; at least kBarMinWidth)
constexpr int32_t kBarMinWidth = 6;

/// The picture pixels that stand for one CSS pixel of the page (the canvas is shown smaller than it is: a phone). From this many on, the bars are made thicker, so that they stay seen.
constexpr double kSmallShownFrom = 2.0;     // shown at less than half of its size
constexpr bool thick_for(double picture_px_per_css) noexcept { return picture_px_per_css > kSmallShownFrom; }

/// The number on a small canvas (a phone). The original's digits are 8 x 15 pixels at every zoom, which is 2 or 3 CSS pixels tall on a phone, so from `kPhoneFrom` picture pixels per CSS pixel the font is
/// drawn `digit_scale` times as big and `digit_outlined` says it wears a black outline of one scaled font pixel (about 1 CSS pixel) too. The picture pixels per CSS pixel are the browser page's (a phone, a tablet, or a
/// narrow desktop browser window); the native build never asks (Renderer::set_health_marks, scale_digits false), so a native window of any size keeps the original's digits. At less than `kPhoneFrom` both are the original's: 1, none.
constexpr double kPhoneFrom = 1.25;
constexpr double kDigitCssFactor = 0.8;     // the scale is the smallest whole number that makes the glyph (10 font pixels tall) about 8 CSS pixels tall: ceil(0.8 x picture pixels per CSS pixel)
constexpr int kDigitScaleMax = 4;
constexpr bool digit_outlined(double picture_px_per_css) noexcept { return picture_px_per_css >= kPhoneFrom; }
inline int digit_scale(double picture_px_per_css) noexcept {
    if (!digit_outlined(picture_px_per_css)) return 1;                       // (also a NaN, which no comparison holds)
    return static_cast<int>(std::clamp(std::ceil(kDigitCssFactor * picture_px_per_css), 1.0, static_cast<double>(kDigitScaleMax)));     // (clamped as a double: an infinite density is no int)
}

/// The bar's height in pixels: 5 from the zoom 1.5, 3 from 0.6, 2 below; one and a half times that (rounded up) on a canvas that is shown small
constexpr double kBarMiddleFrom = 0.6;      // (not the numbers' edge: the bars' sizes are what they were)
constexpr int32_t bar_height(double zoom, bool thick) noexcept {
    const int32_t h = zoom >= 1.5 ? 5 : (zoom >= kBarMiddleFrom ? 3 : 2);
    return thick ? (h * 3 + 1) / 2 : h;
}

struct Bar {
    Rect outline{};     // the black frame around the track (a bar of 3 pixels or more has one); empty when there is none
    Rect track{};       // the dark part that the hit points that are missing show
    Rect fill{};        // the colored part: the hit points, in the track's width
    Color color{};
    bool framed{false};
};

/// The bar of an ant whose position is at (sx, sy) on the screen. The fill is hp / max_hp of the track's width, at least one pixel; `max_hp` below `hp` (or none) is a full bar.
inline Bar bar(int32_t sx, int32_t sy, int hp, int max_hp, double zoom, bool thick) noexcept {
    Bar b;
    const int32_t w = std::max<int32_t>(kBarMinWidth, static_cast<int32_t>(std::lround(static_cast<double>(kBarWorldWidth) * zoom)));
    const int32_t h = bar_height(zoom, thick);
    const int32_t x0 = sx - w / 2;
    const int32_t y0 = sy + static_cast<int32_t>(std::lround(static_cast<double>(-kHeadAbove) * zoom)) - h;
    const int32_t most = std::max(std::max(max_hp, hp), 1);
    const int32_t fw = std::clamp<int32_t>(static_cast<int32_t>(std::lround(static_cast<double>(w) * static_cast<double>(std::max(hp, 0)) / static_cast<double>(most))), 1, w);
    b.framed = h >= 3;
    b.track = Rect{x0, y0, w, h};
    b.outline = b.framed ? Rect{x0 - 1, y0 - 1, w + 2, h + 2} : Rect{x0, y0, 0, 0};
    b.fill = Rect{x0, y0, fw, h};
    b.color = color(band(hp));
    return b;
}

}  // namespace ants::app::health
