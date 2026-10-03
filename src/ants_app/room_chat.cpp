#include "ants_app/room_chat.hpp"

namespace ants::app {

void RoomChatInput::open(bool from_key) {
    open_ = true;
    swallow_ = from_key;
    text_.clear();
}

void RoomChatInput::close() noexcept {
    open_ = false;
    swallow_ = false;
    text_.clear();
}

RoomChatInput::Result RoomChatInput::on_key(SDL_Keycode key, uint16_t modifiers) {
    if (!open_) return Result::None;
    switch (key) {
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            return Result::Send;                       // (the application takes the line: take_line() closes the input; an empty line is sent as nothing)
        case SDLK_ESCAPE:
            close();
            return Result::Closed;
        case SDLK_BACKSPACE:
            if ((modifiers & (KMOD_CTRL | KMOD_GUI)) != 0) text_.clear();
            else if (!text_.empty()) text_.pop_back();
            return Result::None;
        default:
            return Result::None;
    }
}

void RoomChatInput::on_text(const std::string& text) {
    if (!open_) return;
    if (swallow_) {
        swallow_ = false;
        if (text == "t" || text == "T") return;        // the T that opened the input
    }
    for (const char c : text) {
        if (c < 0x20 || c > 0x7E) continue;            // printable ASCII, as the match's chat line (net::decode(ChatMsg) refuses the rest)
        if (text_.size() >= kMaxChars) break;
        text_.push_back(c);
    }
}

std::string RoomChatInput::take_line() {
    std::string line = text_;
    close();
    return line;
}

std::vector<std::string> RoomChatLog::take(const std::vector<net::ChatLine>& lines, uint32_t now_ms) {
    std::vector<std::string> out;
    for (const net::ChatLine& line : lines) {
        if (budget_.take(now_ms, kBurst, kPerSecond)) out.push_back((line.notice() ? std::string("(room)") : line.name) + ": " + line.text);
        else ++left_out_;
    }
    if (left_out_ > 0 && budget_.take(now_ms, kBurst, kPerSecond)) {
        out.push_back("(" + std::to_string(left_out_) + " more lines were not logged)");
        left_out_ = 0;
    }
    return out;
}

}  // namespace ants::app
