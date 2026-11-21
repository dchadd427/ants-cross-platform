#pragma once

// The list of connections in a match of a network game (docs/MULTIPLAYER.md "Ping and delay", docs/VIEW_AND_HUD.md "The connections list"): a small dark panel at the top right of the map view that a click
// or a tap on the corner's "ping" text opens and closes, with one row for each person and computer player of the match: a square in the color of the connection's quality, the name, the round trip
// and the small word. Your own row is your ping to the server ("server"); another person's is the round trip over the channel to that person ("relay" or "direct"), "-- ms" while there is none; a
// computer player has no number. Numbers only: never an address.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "ants_app/countdown_card.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/screen_layout.hpp"

namespace ants::app {

/// One row of the list
struct ConnectionRow {
    std::string name;
    std::string tag;                   // "server" (your own), "direct", "relay", "in" (no channel yet) or a computer player's level ("bot, Hard")
    std::string ping;                  // "38 ms", "-- ms" or "" (a computer player)
    assets::ColorRGBA color;           // of the square and the number (ping_color)
};

/// The rows for the colors that play (`seats` as the countdown card holds them; `mine` is this player's seat, whose number is the ping to the server)
std::vector<ConnectionRow> connection_rows(const std::array<CountdownSeat, 4>& seats, uint8_t mine);

struct ConnectionsLayout {
    LayoutRect panel;                  // the whole list, in the picture's pixels
    int32_t title_y{0};                // top of the title's line ("Connections")
    int32_t first_row_y{0};            // top of the first row's line
    int32_t row_pitch{22};
    int32_t note_y{0};                 // top of the two lines of the note under the rows
    int32_t name_x{0};                 // left edges of the square, the name; the right edges of the number and the small word
    int32_t square_x{0};
    int32_t ping_right{0};
    int32_t tag_right{0};
};

/// Where the list goes for `rows` rows at the top right of `view` (the map view), 10 pixels from its top and right edges (a view too narrow for it puts it at the view's left)
ConnectionsLayout connections_layout(const LayoutRect& view, size_t rows);

/// Draws the list over the picture
void draw_connections(IRenderer& renderer, const ConnectionsLayout& layout, const std::vector<ConnectionRow>& rows);

}  // namespace ants::app
