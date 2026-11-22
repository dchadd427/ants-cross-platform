#include "ants_app/connections_overlay.hpp"

#include <algorithm>

namespace ants::app {

namespace {
constexpr int32_t kPanelW = 232;
constexpr int32_t kPadX = 10;
constexpr int32_t kTitleH = 26;
constexpr int32_t kNoteH = 30;
}  // namespace

std::vector<ConnectionRow> connection_rows(const std::array<CountdownSeat, 4>& seats, uint8_t mine) {
    static const char* const kColors[4] = {"Green", "Red", "Blue", "Black"};
    std::vector<ConnectionRow> rows;
    for (size_t seat = 0; seat < seats.size(); ++seat) {
        const CountdownSeat& who = seats[seat];
        if (!who.plays) continue;
        const CountdownRowWords words = countdown_row_words(who, seat == mine && !who.computer, kColors[seat]);
        ConnectionRow row;
        row.name = words.name;
        row.tag = words.tag;
        row.ping = words.ping;
        row.color = who.computer ? ping_color(-1) : ping_color(who.ping_ms >= 0 ? who.ping_ms : -1);
        rows.push_back(row);
    }
    return rows;
}

ConnectionsLayout connections_layout(const LayoutRect& view, size_t rows) {
    ConnectionsLayout l;
    const int32_t h = kTitleH + static_cast<int32_t>(rows) * l.row_pitch + 6 + kNoteH + 6;
    const int32_t x = std::max(view.x, view.right() - 10 - kPanelW);
    l.panel = LayoutRect{x, view.y + 10, kPanelW, h};
    l.title_y = l.panel.y + 6;
    l.first_row_y = l.panel.y + kTitleH + 2;
    l.note_y = l.first_row_y + static_cast<int32_t>(rows) * l.row_pitch + 4;
    l.square_x = l.panel.x + kPadX;
    l.name_x = l.panel.x + kPadX + 18;
    l.ping_right = l.panel.x + 150;
    l.tag_right = l.panel.right() - kPadX;
    return l;
}

void draw_connections(IRenderer& renderer, const ConnectionsLayout& l, const std::vector<ConnectionRow>& rows) {
    using assets::ColorRGBA;
    const ColorRGBA cream{244, 236, 226, 255};
    const ColorRGBA dim{168, 194, 184, 255};
    const LayoutRect& p = l.panel;
    renderer.fill_rect(p.x, p.y, p.w, p.h, ColorRGBA{8, 18, 14, 214});
    renderer.draw_rect(p.x, p.y, p.w, p.h, ColorRGBA{43, 99, 87, 255});
    renderer.draw_text("Connections", l.square_x, l.title_y, cream, FontSize::Px14);
    const int32_t ping_w = renderer.get_text_width("ping", FontSize::Px12);
    renderer.draw_text("ping", l.tag_right - ping_w, l.title_y + 1, dim, FontSize::Px12);
    renderer.fill_rect(p.x + kPadX, p.y + kTitleH - 2, p.w - 2 * kPadX, 1, ColorRGBA{43, 99, 87, 255});
    int32_t y = l.first_row_y;
    for (const ConnectionRow& row : rows) {
        renderer.fill_rect(l.square_x, y + 3, 9, 9, ColorRGBA{2, 8, 6, 255});
        renderer.fill_rect(l.square_x + 1, y + 4, 7, 7, row.color);
        const int32_t tag_w = renderer.get_text_width(row.tag, FontSize::Px12);
        const int32_t ping_w_row = renderer.get_text_width(row.ping, FontSize::Px14);
        renderer.draw_text_squeezed(row.name, l.name_x, y, cream, FontSize::Px14, std::max(10, l.ping_right - ping_w_row - 8 - l.name_x));
        renderer.draw_text(row.ping, l.ping_right - ping_w_row, y, row.color, FontSize::Px14);
        renderer.draw_text(row.tag, l.tag_right - tag_w, y + 2, dim, FontSize::Px12);
        y += l.row_pitch;
    }
    renderer.draw_text("you: to the server", l.square_x, l.note_y, dim, FontSize::Px12);
    renderer.draw_text("others: to them, relay or direct", l.square_x, l.note_y + 14, dim, FontSize::Px12);
}

}  // namespace ants::app
