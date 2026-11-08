// The chat of the match screen (hud.hpp): the entries and their lines, the news flashes, the scroll and the drag of the chat box, its drawing and the transcript that is written when the program ends.
#include "ants_app/hud.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/text_layout.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

namespace ants::app {

namespace {

constexpr size_t kChatBodyMaxChars = 100;
constexpr size_t kChatHeaderMaxChars = 50;      // the header text object holds 50 characters

// What measures the text when no renderer was given (every character 6 px wide: the IRenderer defaults)
class DefaultTextMetrics final : public IRenderer {
public:
    void draw_sprite(uint32_t, int32_t, int32_t, bool) override {}
    void draw_named_sprite(const std::string&, int32_t, int32_t, bool) override {}
    void fill_rect(int32_t, int32_t, int32_t, int32_t, assets::ColorRGBA) override {}
    void draw_rect(int32_t, int32_t, int32_t, int32_t, assets::ColorRGBA) override {}
    void draw_text(const std::string&, int32_t, int32_t, assets::ColorRGBA) override {}
    void set_hud_team(uint8_t) override {}
};

const IRenderer& default_text_metrics() {
    static const DefaultTextMetrics metrics;
    return metrics;
}

}  // namespace

void HUD::add_chat_entry(const std::string& sender, const std::string& message, bool team_only, int color_index) {
    const uint8_t color = static_cast<uint8_t>(color_index >= 0 && color_index <= 3 ? color_index : (3 - (local_player_id_ & 3)));
    push_chat_entry(sender + (team_only ? " (To Teammate):" : ":"), message, color);
}

void HUD::set_text_metrics(const IRenderer* metrics) {
    if (text_metrics_ == metrics) return;
    text_metrics_ = metrics;
    relayout_chat();
}

// The labels of an entry (FUN_010123e2): the header is one line (no wrap), the body wraps at 126 px; both are 12 px labels. The entry starts at `top`, the body under the header.
void HUD::layout_chat_entry(ChatEntry& entry, int32_t top) const {
    const IRenderer& metrics = text_metrics_ != nullptr ? *text_metrics_ : default_text_metrics();
    const int32_t line = font_cell_height(FontSize::Px12);
    entry.top = top;
    entry.header_h = line;
    entry.body_lines = wrap_label_text(metrics, entry.body, kChatBodyW, FontSize::Px12);
    entry.body_h = static_cast<int32_t>(entry.body_lines.size()) * line;
}

void HUD::append_chat_display_lines(const ChatEntry& entry) {
    chat_log_.push_back(entry.header);
    chat_line_color_.push_back(entry.color);
    for (const std::string& line : entry.body_lines) {
        chat_log_.push_back(line);
        chat_line_color_.push_back(5);
    }
}

// The measure changed: every label is measured again and the entries are restacked; the window's positions stay inside the new log. When the view changed its height too (`old_view_h`, the
// picture's shape changed) a window that was showing the newest line still shows it (a shorter view would otherwise leave the last rows below it)
void HUD::relayout_chat(int32_t old_view_h) {
    const int32_t old_bottom = std::max(0, chat_content_end_ - 1 - old_view_h);                    // (where AddLine puts the window: the newest line on its last row)
    const bool pos_at_bottom = old_view_h >= 0 && chat_follow_pos_ >= old_bottom;
    const bool target_at_bottom = old_view_h >= 0 && chat_follow_target_ >= old_bottom;
    chat_log_.clear();
    chat_line_color_.clear();
    int32_t top = 0;
    for (ChatEntry& entry : chat_entries_) {
        layout_chat_entry(entry, top);
        append_chat_display_lines(entry);
        top = entry.bottom() + 1;
    }
    chat_content_end_ = top;
    const int32_t limit = std::max(0, chat_content_end_ - layout_.chat_view().h);
    const int32_t bottom = std::max(0, chat_content_end_ - 1 - layout_.chat_view().h);
    chat_follow_pos_ = pos_at_bottom ? bottom : std::min(chat_follow_pos_, limit);
    chat_follow_target_ = target_at_bottom ? bottom : std::min(chat_follow_target_, limit);
    chat_drag_offset_ = std::min(chat_drag_offset_, limit);
}

// AddLine (0x10120e9): the entry is made at the end of the log (`+0x2c`), the end moves to its bottom + 1, and when the end is now below the window the window's target is the
// position that puts the new bottom on the window's last row; the follow task (CHATAPPD) is started unless it is running (the window is not at its target)
void HUD::push_chat_entry(std::string header, const std::string& message, uint8_t color) {
    if (header.size() > kChatHeaderMaxChars) header.resize(kChatHeaderMaxChars);
    ChatEntry entry;
    entry.header = std::move(header);
    entry.body = message.substr(0, kChatBodyMaxChars);
    entry.color = color;
    layout_chat_entry(entry, chat_content_end_);
    append_chat_display_lines(entry);
    const int32_t bottom = entry.bottom();
    chat_content_end_ = bottom + 1;
    chat_entries_.push_back(std::move(entry));
    const int32_t view_h = layout_.chat_view().h;
    if (view_h < chat_content_end_ - chat_follow_pos_) {
        if (chat_follow_pos_ == chat_follow_target_) {
            chat_follow_task_ = true;
            chat_follow_due_ms_ = clock_ms();                   // AddTask(task, 0, 50 ms, 0): the first pass is the next one
        }
        chat_follow_target_ = bottom - view_h;
    }
}

void HUD::add_news_flash(uint32_t elapsed_ms, const std::string& text) {
    const uint32_t secs = elapsed_ms / 1000;
    char header[48];
    std::snprintf(header, sizeof(header), "[%u:%02u] News Flash:", secs / 60, secs % 60);
    push_chat_entry(header, text, 4);
}

void HUD::receive_chat_message(uint8_t sender, const std::string& name, const std::string& text, bool to_team, const sim::WorldState& world) {
    if (!options_.state().chat) return;                                       // the receive handler drops it (0x102411a)
    if (to_team && sender != local_player_id_) {
        const bool sender_ally_is_me = sender < world.player_alliances.size() && world.player_alliances[sender] == local_player_id_;
        if (!sender_ally_is_me) return;                               // team text: only the sender and the players whose ally the sender is
    }
    add_chat_entry(name, text, to_team, 3 - (sender & 3));
}

// CHATAPPD (0x1025282, every 50 ms) and CHATSCRL (0x1025234, every 100 ms) run on the list scheduler, which runs a task at the next pass after it is due and
// then a period later
void HUD::update_chat_tasks() {
    const uint32_t now = clock_ms();
    if (chat_follow_task_ && static_cast<int32_t>(now - chat_follow_due_ms_) >= 0) {
        if (chat_follow_pos_ == chat_follow_target_) {
            chat_follow_task_ = false;                                  // the task returns 0 and is removed
        } else {
            // the position moves toward the target by at most 5 px per pass
            const int32_t step = std::min(kChatFollowStep, std::abs(chat_follow_target_ - chat_follow_pos_));
            chat_follow_pos_ += chat_follow_target_ > chat_follow_pos_ ? step : -step;
            chat_follow_due_ms_ = now + kChatFollowPeriodMs;
        }
    }
    if (chat_scroll_task_ && static_cast<int32_t>(now - chat_scroll_due_ms_) >= 0) {
        // the pointer of the last press or drag event: outside the view the log scrolls 15 px per pass (up while it is above the view, down from below)
        if (!in_chat_view(chat_drag_x_, chat_drag_y_)) chat_scroll_by(chat_drag_y_ >= layout_.chat_view().y ? -kChatScrollStep : kChatScrollStep);
        chat_scroll_due_ms_ = now + kChatScrollPeriodMs;
    }
}

// 0x101228a: the dragged position moves by `delta` inside the log (only when the log is at least as high as the view)
void HUD::chat_scroll_by(int32_t delta) noexcept {
    const int32_t view_h = layout_.chat_view().h;
    if (view_h <= chat_content_end_) {
        const int32_t room_below = (chat_content_end_ - view_h) - chat_drag_offset_;
        const int32_t room_above = -chat_drag_offset_;
        const int32_t d = std::min(std::max(delta, room_above), room_below);
        chat_drag_offset_ += d;
    }
}

// 0x1012015, a left press: inside the view the drag starts from the window's current position and CHATSCRL is scheduled
void HUD::start_chat_drag(int32_t x, int32_t y) {
    chat_drag_x_ = x;
    chat_drag_y_ = y;
    chat_drag_offset_ = chat_follow_pos_;
    chat_dragging_ = true;
    if (!chat_scroll_task_) {
        chat_scroll_task_ = true;
        chat_scroll_due_ms_ = clock_ms();
    }
}

// 0x1012096: while dragging, the pointer is remembered and a move inside the view scrolls the log by the distance the pointer went up
void HUD::move_chat_drag(int32_t x, int32_t y) {
    if (!chat_dragging_) return;
    const int32_t delta = chat_drag_y_ - y;
    chat_drag_x_ = x;
    chat_drag_y_ = y;
    if (in_chat_view(x, y)) chat_scroll_by(delta);
}

// 0x1012015, the release: the log shows the window that follows the newest entry again
void HUD::end_chat_drag() noexcept {
    chat_dragging_ = false;
    chat_scroll_task_ = false;
}

// FUN_01012190: the entries that reach into the view are drawn at their place, the header at the left edge and the body 10 px right of it, clipped to the view
void HUD::render_chat_log(IRenderer& renderer) {
    static const assets::ColorRGBA kChatColors[6] = {
        {39, 39, 59, 255}, {43, 39, 107, 255}, {119, 0, 0, 255}, {7, 67, 47, 255}, {79, 0, 143, 255}, {7, 11, 15, 255}};
    const int32_t offset = chat_view_offset();
    const int32_t line = font_cell_height(FontSize::Px12);
    const LayoutRect view = layout_.chat_view();
    renderer.set_clip_rect(view.x, view.y, view.w, view.h);
    for (const ChatEntry& entry : chat_entries_) {
        if (entry.bottom() <= offset) continue;
        if (entry.top >= offset + view.h) break;
        const int32_t y = view.y + entry.top - offset;
        draw_single_line_label(renderer, entry.header, view.x, y, view.w, true, kChatColors[std::min<size_t>(entry.color, 5)], FontSize::Px12);
        int32_t body_y = y + entry.header_h;
        for (const std::string& body_line : entry.body_lines) {
            renderer.draw_text(body_line, view.x + kChatBodyX, body_y, kChatColors[5], FontSize::Px12);
            body_y += line;
        }
    }
    renderer.clear_clip_rect();
}

// What the original writes into chat.txt when the program ends (0x10122d4): "%s @ %s\n\n" with the date and the time, then "%s %s\n" for every entry
std::string HUD::chat_transcript(const std::string& date_time) const {
    std::string out = date_time + "\n\n";
    for (const ChatEntry& entry : chat_entries_) out += entry.header + " " + entry.body + "\n";
    return out;
}

} // namespace ants::app
