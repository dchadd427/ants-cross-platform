#pragma once

// The network's share of the bottom right corner: "ping NN ms" (the round trip to the host; a dash when the last answer is older than three seconds) and "delay NN ms" (the
// median over the last five commands of the time from sending one of the player's own commands to the tick that applies it; a dash when the last command was applied more than ten
// seconds ago, see ants_net/latency.hpp), in the style and size of
// the frame rate counter next to which they stand (12 px, white, no plate of their own). They
// are shown in the room of a network game and during its match (never in a game of one machine) and are a remake-only overlay like the counter: the original draws nothing
// there.
//
// Where they stand depends on the room the screen leaves. Every screen's corner row is [version] [sparkline] [frame rate] from x = 514 to 632 (see fps_overlay.hpp), and
// the row has the whole width of the screen to its left on the results screen and about 170 pixels on the setup screen, so there the two texts stand in the same row, to
// the left of the version. In a match the bottom row of the screen is the score boxes of the other teams (up to x = 458) and the row's free part is 50 pixels, so the two
// texts stand on two lines above the row, right aligned with the frame rate. (The area above the row is the "Send to: All" button, x 532 .. 575, and the chat box: the lines keep
// to x >= 563 and only touch the right rim of the button's box, by 7 pixels for a three digit delay and by none for one or two digits.) A layout never changes with the numbers:
// the choice is made with the widest possible texts.

#include <cstdint>
#include <optional>
#include <string>

#include "ants_app/renderer.hpp"
#include "ants_net/netgame.hpp"

namespace ants::app {

/// What the corner shows of the network; an empty value is shown as "-" (nothing measured yet, no command applied yet for ten seconds, or a ping reading older than three seconds)
struct LatencyReadout {
    std::optional<uint32_t> ping_ms;
    std::optional<uint32_t> delay_ms;
};

/// The most that is shown: a number of four digits ("ping 9999 ms"), so that the texts have a bounded width
inline constexpr uint32_t LATENCY_SHOWN_MAX_MS = 9999;

/// "ping 42 ms", "ping -" while nothing is measured; the number is the round trip in ms (at most LATENCY_SHOWN_MAX_MS)
std::string ping_text(const std::optional<uint32_t>& ping_ms);
/// "delay 230 ms", "delay -" while no command of the player has been applied; at most LATENCY_SHOWN_MAX_MS
std::string delay_text(const std::optional<uint32_t>& delay_ms);

/// What the corner row stands on: the screens differ in what is to the left of the version
enum class CornerScreen : uint8_t {
    Other,      // the loading screen, the quick help: no network game is shown on them
    Setup,      // the setup screen, which is the room of a network game
    Match,      // the match screen (the HUD, with its dialogs)
    Results     // the results screen that a match ends with
};

/// The leftmost x that the two texts may use in a row on each kind of screen (the x of the first pixel of the first text). The setup screen's bottom row begins with the
/// prompt box (36 .. 329, its picture to 336); in a match the bottom row holds the score boxes up to the cover of the third slot (402 - 2 + 58 = 458); the results screen's
/// lists end at row 465, nothing stands in the corner row.
inline constexpr int32_t LATENCY_LEFT_LIMIT_SETUP = 340;
inline constexpr int32_t LATENCY_LEFT_LIMIT_MATCH = 460;
inline constexpr int32_t LATENCY_LEFT_LIMIT_RESULTS = 8;

/// Whether the corner shows the network's readout on `screen`, and the leftmost x of its row there: only in the room (Room, and Loading while the map loads) and the match of
/// a network game, on the setup, match and results screens. Nothing for a game of one machine, a connection that is being made, one that failed or is over, or any other screen.
std::optional<int32_t> latency_left_limit(bool network_active, net::NetGame::Phase phase, CornerScreen screen);

/// The right end of both layouts: the right edge of the frame rate text (the corner's margin)
inline constexpr int32_t LATENCY_RIGHT_EDGE = 632;
/// Between two texts of a row, and between the row and the version
inline constexpr int32_t LATENCY_TEXT_GAP = 8;
inline constexpr int32_t LATENCY_VERSION_GAP = 6;

struct LatencyCornerLayout {
    bool stacked{false};            // false: one row to the left of the version; true: two lines above the corner row
    int32_t ping_x{0};              // the top left of each text (draw_text's origin)
    int32_t ping_y{0};
    int32_t delay_x{0};
    int32_t delay_y{0};
};

/// Where the two texts go. `ping_w` / `delay_w` are the widths of the texts as they are now, `widest_w` that of both texts at their widest (ping_text(9999) and
/// delay_text(9999) and the gap between them: the choice of the layout is made with it, so that it never flips with the numbers), `text_h` the height of a 12 px text,
/// `version_x` the left edge of the version text and `text_y` the top of the corner row's texts (the frame rate's), `left_limit` one of the LATENCY_LEFT_LIMIT_ values.
LatencyCornerLayout layout_latency_corner(int32_t ping_w, int32_t delay_w, int32_t widest_w, int32_t text_h, int32_t version_x, int32_t text_y, int32_t left_limit);

/// Draws the readout (the calls are the two texts and nothing else: no plate, no frame)
void draw_latency_corner(IRenderer& renderer, const LatencyReadout& readout, int32_t version_x, int32_t text_y, int32_t left_limit);

}  // namespace ants::app
