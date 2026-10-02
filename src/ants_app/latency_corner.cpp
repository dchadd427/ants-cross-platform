#include "ants_app/latency_corner.hpp"

#include <algorithm>

#include "ants_app/fps_overlay.hpp"

namespace ants::app {

std::string ping_text(const std::optional<uint32_t>& ping_ms) {
    if (!ping_ms) return "ping -";
    return "ping " + std::to_string(std::min(*ping_ms, LATENCY_SHOWN_MAX_MS)) + " ms";
}

std::string delay_text(const std::optional<uint32_t>& delay_ms) {
    if (!delay_ms) return "delay -";
    return "delay " + std::to_string(std::min(*delay_ms, LATENCY_SHOWN_MAX_MS)) + " ms";
}

std::optional<int32_t> latency_left_limit(bool network_active, net::NetGame::Phase phase, CornerScreen screen, const ScreenLayout& layout) {
    if (!network_active) return std::nullopt;                                          // a game of one machine draws nothing new
    if (phase != net::NetGame::Phase::Room && phase != net::NetGame::Phase::Loading && phase != net::NetGame::Phase::Playing) return std::nullopt;
    switch (screen) {
        case CornerScreen::Setup: return LATENCY_LEFT_LIMIT_SETUP;
        case CornerScreen::Match: return layout.score_row_right();
        case CornerScreen::Results: return LATENCY_LEFT_LIMIT_RESULTS;
        case CornerScreen::Other: break;
    }
    return std::nullopt;
}

std::optional<int32_t> latency_left_limit(bool network_active, net::NetGame::Phase phase, CornerScreen screen) {
    return latency_left_limit(network_active, phase, screen, ScreenLayout::classic());
}

LatencyCornerLayout layout_latency_corner(int32_t ping_w, int32_t delay_w, int32_t widest_w, int32_t text_h, int32_t version_x, int32_t text_y, int32_t left_limit,
                                          const CornerPlate& corner) {
    LatencyCornerLayout layout;
    const int32_t row_right = version_x - LATENCY_VERSION_GAP;             // where a row of texts would end: just left of the version
    layout.stacked = row_right - widest_w < left_limit;                    // (decided with the widest texts: the layout does not move with the numbers)
    if (!layout.stacked) {
        layout.delay_x = row_right - delay_w;
        layout.ping_x = layout.delay_x - LATENCY_TEXT_GAP - ping_w;
        layout.ping_y = text_y;
        layout.delay_y = text_y;
    } else {
        layout.ping_x = corner.right_edge - ping_w;                        // two lines above the corner row, right aligned with the frame rate; the row's plate begins
        layout.delay_x = corner.right_edge - delay_w;                      // at the plate's top and one clear row separates it from the text
        layout.delay_y = corner.top - 1 - text_h;
        layout.ping_y = layout.delay_y - text_h;
    }
    return layout;
}

LatencyCornerLayout layout_latency_corner(int32_t ping_w, int32_t delay_w, int32_t widest_w, int32_t text_h, int32_t version_x, int32_t text_y, int32_t left_limit) {
    return layout_latency_corner(ping_w, delay_w, widest_w, text_h, version_x, text_y, left_limit, CornerPlate::classic());
}

LatencyCornerLayout latency_corner_layout(const IRenderer& renderer, const LatencyReadout& readout, int32_t version_x, int32_t text_y, int32_t left_limit, const CornerPlate& corner) {
    const std::string ping = ping_text(readout.ping_ms);
    const std::string delay = delay_text(readout.delay_ms);
    const int32_t widest = renderer.get_text_width(ping_text(LATENCY_SHOWN_MAX_MS), FontSize::Px12) + LATENCY_TEXT_GAP +
                           renderer.get_text_width(delay_text(LATENCY_SHOWN_MAX_MS), FontSize::Px12);
    return layout_latency_corner(renderer.get_text_width(ping, FontSize::Px12), renderer.get_text_width(delay, FontSize::Px12), widest,
                                 renderer.get_text_height(FontSize::Px12), version_x, text_y, left_limit, corner);
}

void draw_latency_corner(IRenderer& renderer, const LatencyReadout& readout, int32_t version_x, int32_t text_y, int32_t left_limit, const CornerPlate& corner) {
    const std::string ping = ping_text(readout.ping_ms);
    const std::string delay = delay_text(readout.delay_ms);
    const LatencyCornerLayout layout = latency_corner_layout(renderer, readout, version_x, text_y, left_limit, corner);
    const ants::assets::ColorRGBA white{255, 255, 255, 255};              // the frame rate's colour
    renderer.draw_text(ping, layout.ping_x, layout.ping_y, white, FontSize::Px12);
    renderer.draw_text(delay, layout.delay_x, layout.delay_y, white, FontSize::Px12);
}

void draw_latency_corner(IRenderer& renderer, const LatencyReadout& readout, int32_t version_x, int32_t text_y, int32_t left_limit) {
    draw_latency_corner(renderer, readout, version_x, text_y, left_limit, CornerPlate::classic());
}

}  // namespace ants::app
