#include "ants_app/net_overlay.hpp"

#include <algorithm>

#include "ants_sim/game_strings.hpp"

namespace ants::app {

namespace {

// The name of a seat in a line: its own, else "Player 3" (the same words as the lag notice)
std::string player_text(const std::string& name, int seat) { return name.empty() ? "Player " + std::to_string(seat + 1) : name; }

// "Red" for seat 1 (the seats are green, red, blue, black)
std::string colour_text(int seat) { return seat >= 0 && seat < 4 ? sim::strings::colour_name(static_cast<uint8_t>(3 - seat)) : std::string(); }

bool fits(const NetOverlayInput& in, const std::string& text, int32_t max_width) { return !in.measure || max_width <= 0 || in.measure(text) <= max_width; }

// before + name + after, the name cut and ended with "..." until the whole fits `max_width` (names are printable ASCII: a byte is a character)
std::string with_name(const NetOverlayInput& in, const std::string& before, const std::string& name, const std::string& after, int32_t max_width) {
    std::string text = before + name + after;
    if (fits(in, text, max_width)) return text;
    std::string cut = name;
    while (!cut.empty()) {
        cut.pop_back();
        text = before + cut + "..." + after;
        if (fits(in, text, max_width)) return text;
    }
    return before + "..." + after;
}

// "Bob (Red) lost the connection, waiting 0:42", or "Bob (Red) is coming back... 45%"
std::string missing_line(const NetOverlayInput& in, const NetOverlaySeat& seat) {
    const std::string name = player_text(seat.name, seat.seat);
    const std::string colour = colour_text(seat.seat);
    const std::string tag = colour.empty() ? std::string() : " (" + colour + ")";
    if (seat.catching_up) return with_name(in, "", name, tag + " is coming back... " + std::to_string(std::min<unsigned>(seat.progress, 100u)) + "%", in.max_width);
    return with_name(in, "", name, tag + " lost the connection, waiting " + net_overlay_clock(seat.away_s), in.max_width);
}

bool is_missing(const NetOverlayInput& in, uint8_t seat) {
    return std::any_of(in.missing.begin(), in.missing.end(), [seat](const NetOverlaySeat& s) { return s.seat == seat; });
}

void fill_vote(const NetOverlayInput& in, NetOverlayLine& line) {
    NetOverlayVote& v = line.vote;
    v.open = true;
    v.count = std::to_string(static_cast<unsigned>(in.votes_continue)) + " of " + std::to_string(static_cast<unsigned>(in.voters)) + " voted to continue";
    v.keep = "F2 Keep waiting";
    // the two buttons stand in one row: the second label gets what the first leaves (each button has 8 px of padding on both sides, and 8 px lie between them)
    int32_t room = 0;                                                           // (0: no limit; a row that has no room for the second label leaves it 1 px, and the name is gone)
    if (in.measure && in.max_width > 0) room = std::max(1, in.max_width - (in.measure(v.keep) + 16) - 8 - 16);
    v.go_on = with_name(in, "F3 Continue without ", player_text(in.vote_name, in.vote_seat), "", room);
    v.keep_pressed = in.my_vote == NetOverlayInput::Choice::KeepWaiting;
    v.go_on_pressed = in.my_vote == NetOverlayInput::Choice::Continue;
}

}  // namespace

std::string net_overlay_clock(uint32_t seconds) {
    const std::string s = std::to_string(seconds % 60u);
    return std::to_string(seconds / 60u) + ":" + (s.size() < 2 ? "0" : "") + s;
}

NetOverlayLine net_overlay_line(const NetOverlayInput& in) {
    NetOverlayLine line;
    auto done = [&line]() {
        if (!line.lines.empty()) line.text = line.lines.front();
        return line;
    };
    // 1. this machine's own way back
    if (in.reconnecting) {
        std::string text = "Connection lost. Reconnecting... " + net_overlay_clock(in.away_s);
        if (in.attempts >= 2) text += " (attempt " + std::to_string(in.attempts) + ")";
        line.lines = {text, "Esc leaves the match"};
        return done();
    }
    if (in.way_back_catching_up) return line;                                  // (the catch-up screen is drawn instead of the match)

    // 2. and 3. a vote that is open, and the seats that are missing; the missing seats wait a second (a blip is no banner), a vote does not
    const bool show_missing = in.vote_open || in.held_ms >= NET_HELD_MESSAGE_MS;
    if (show_missing) {
        for (size_t i = 0; i < in.missing.size() && i < 3; ++i) line.lines.push_back(missing_line(in, in.missing[i]));
    }
    if (in.vote_open) {
        if (!(show_missing && is_missing(in, in.vote_seat))) {                 // a seat that is back but keeps losing its connection: the block says which one
            const std::string name = player_text(in.vote_name, in.vote_seat);
            const std::string colour = colour_text(in.vote_seat);
            line.lines.push_back(with_name(in, "", name, (colour.empty() ? std::string() : " (" + colour + ")") + " keeps losing the connection", in.max_width));
        }
        fill_vote(in, line);
    }
    if (!line.lines.empty() || line.vote.open) return done();

    // 4. the countdown that follows a pause
    if (in.resume_seconds_left > 0) {
        const std::string seconds = std::to_string(static_cast<unsigned>(in.resume_seconds_left));
        line.lines.push_back(in.back_name.empty() ? "The match goes on in " + seconds : with_name(in, "", in.back_name, " is back: the match goes on in " + seconds, in.max_width));
        return done();
    }

    // 5. today's lines
    std::string text;
    if (in.desynced) {
        text = "Out of sync: the match has stopped.";
        line.alarm = true;
    } else if (in.electing) {
        text = "The host left. Choosing a new host...";
    } else if (in.stalled_ms >= NET_WAIT_MESSAGE_MS) {
        text = in.waiting_for.empty() ? std::string("Waiting for the other players...") : "Waiting for " + in.waiting_for + "...";
    } else if (in.catching_up) {
        text = "Catching up...";
    } else if (in.self_lag_behind_ms > 0) {
        text = "You are lagging (" + std::to_string((in.self_lag_behind_ms + 500u) / 1000u) + " s behind)";
    } else if (in.lag_seat >= 0 && in.lag_seat < 4) {
        const std::string name = in.lag_name.empty() ? "Player " + std::to_string(in.lag_seat + 1) : in.lag_name;
        text = name + " is lagging (" + std::to_string((in.lag_behind_ms + 500u) / 1000u) + " s behind)";
    } else {
        text = in.notice;
    }
    if (!text.empty()) line.lines.push_back(text);
    return done();
}

NetOverlayBox net_overlay_box(const LayoutRect& view, int32_t text_w, int32_t text_h) {
    NetOverlayBox out;
    out.text_x = view.x + 1 + (view.w - 1 - text_w) / 2;
    out.text_y = view.y + 5;
    out.box = LayoutRect{out.text_x - 6, out.text_y - 3, text_w + 12, text_h + 6};
    return out;
}

int32_t net_overlay_max_width(const LayoutRect& view) { return std::max<int32_t>(0, view.w - 2 * 6 - 2 * 4); }

NetOverlayLayout net_overlay_layout(const LayoutRect& view, const NetOverlayMetrics& m, bool with_vote) {
    NetOverlayLayout out;
    const int32_t step = m.text_h + 6 + 2;                                      // a box is the text and 3 px above and below it; 2 px lie between two boxes
    for (size_t i = 0; i < m.line_w.size(); ++i) {
        NetOverlayBox b = net_overlay_box(view, m.line_w[i], m.text_h);
        const int32_t down = static_cast<int32_t>(i) * step;
        b.text_y += down;
        b.box.y += down;
        out.lines.push_back(b);
    }
    if (!with_vote) return out;
    out.has_vote = true;
    out.count = net_overlay_box(view, m.count_w, m.text_h);
    const int32_t rows_down = static_cast<int32_t>(m.line_w.size()) * step;
    out.count.text_y += rows_down;
    out.count.box.y += rows_down;
    constexpr int32_t kPad = 8;                                                 // a button: its label and 8 px on both sides
    constexpr int32_t kGap = 8;                                                 // between the two buttons
    const int32_t keep_w = m.keep_w + 2 * kPad;
    const int32_t go_on_w = m.go_on_w + 2 * kPad;
    const int32_t height = m.text_h + 10;
    const int32_t x0 = view.x + 1 + (view.w - 1 - (keep_w + kGap + go_on_w)) / 2;
    const int32_t y0 = out.count.box.y + out.count.box.h + 2;
    out.keep = LayoutRect{x0, y0, keep_w, height};
    out.go_on = LayoutRect{x0 + keep_w + kGap, y0, go_on_w, height};
    out.keep_text_x = x0 + kPad;
    out.go_on_text_x = x0 + keep_w + kGap + kPad;
    out.button_text_y = y0 + 5;
    return out;
}

}  // namespace ants::app
