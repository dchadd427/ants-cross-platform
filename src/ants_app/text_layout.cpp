#include "ants_app/text_layout.hpp"

#include <algorithm>
#include <cstddef>

namespace ants::app {

namespace {

// One paragraph (no '\n' inside), the loop of FUN_0102b0b5
void wrap_paragraph(const IRenderer& renderer, const std::string& text, int32_t width, FontSize size, std::vector<std::string>& lines) {
    const size_t len = text.size();
    size_t p = 0;
    while (p < len) {
        while (p < len && text[p] == ' ') ++p;                     // leading spaces of a line are skipped
        if (p >= len) break;
        int32_t last_word_end = 0;                                 // index (from the line start) of the last word end seen: 0 = none
        int32_t count = 0;                                         // characters scanned so far
        size_t q = p;
        while (q < len) {
            if (renderer.get_text_width(text.substr(p, q - p + 1), size) >= width) break;      // the text so far has reached the width
            if (text[q] != ' ' && (q + 1 >= len || text[q + 1] == ' ')) last_word_end = count;
            ++count;
            ++q;
        }
        if (last_word_end == 0) last_word_end = count - 1;         // no word end seen: cut one character before the width was reached
        const size_t n = static_cast<size_t>(std::max(1, last_word_end + 1));      // (the original would loop forever on an empty cut)
        lines.push_back(text.substr(p, n));
        p += n;
    }
}

}  // namespace

std::vector<std::string> wrap_label_text(const IRenderer& renderer, const std::string& text, int32_t width, FontSize size) {
    std::vector<std::string> lines;
    if (width <= 0) {
        if (!text.empty()) lines.push_back(text);
        return lines;
    }
    size_t start = 0;
    while (start <= text.size()) {
        const size_t next = text.find('\n', start);
        wrap_paragraph(renderer, text.substr(start, next == std::string::npos ? std::string::npos : next - start), width, size, lines);
        if (next == std::string::npos) break;
        start = next + 1;
    }
    return lines;
}

std::string fit_text(const IRenderer& renderer, std::string text, int32_t width, FontSize size) {
    while (text.size() > 1 && renderer.get_text_width(text, size) > width) text.pop_back();
    return text;
}

int32_t draw_label(IRenderer& renderer, const std::string& text, int32_t x, int32_t y, int32_t width, ants::assets::ColorRGBA color, FontSize size,
                   bool center) {
    const int32_t pitch = font_cell_height(size);
    int32_t line_y = y;
    for (const std::string& line : wrap_label_text(renderer, text, width, size)) {
        const int32_t line_x = center ? x + (width - renderer.get_text_width(line, size)) / 2 : x;
        renderer.draw_text(line, line_x, line_y, color, size);
        line_y += pitch;
    }
    return line_y - y;
}

std::vector<std::string> wrap_label_fitted(const IRenderer& renderer, const std::string& text, int32_t width, FontSize size, size_t max_lines) {
    std::vector<std::string> lines = wrap_label_text(renderer, text, width, size);
    if (lines.size() <= max_lines || max_lines == 0) {
        if (max_lines == 0) lines.clear();
        return lines;
    }
    lines.resize(max_lines);
    std::string& last = lines.back();
    static const std::string kDots = "...";
    while (!last.empty() && renderer.get_text_width(last + kDots, size) > width) last.pop_back();       // (what is left of the line, and the dots behind it, fit the box)
    while (!last.empty() && last.back() == ' ') last.pop_back();
    last += kDots;
    return lines;
}

std::vector<std::string> wrap_label_tail(const IRenderer& renderer, const std::string& text, int32_t width, FontSize size, size_t max_lines) {
    std::vector<std::string> lines = wrap_label_text(renderer, text, width, size);
    if (lines.size() > max_lines) lines.erase(lines.begin(), lines.begin() + static_cast<std::ptrdiff_t>(lines.size() - max_lines));
    return lines;
}

int32_t draw_label_lines(IRenderer& renderer, const std::vector<std::string>& lines, int32_t x, int32_t y, ants::assets::ColorRGBA color, FontSize size) {
    const int32_t pitch = font_cell_height(size);
    int32_t line_y = y;
    for (const std::string& line : lines) {
        renderer.draw_text(line, x, line_y, color, size);
        line_y += pitch;
    }
    return line_y - y;
}

void draw_edit_line(IRenderer& renderer, const std::string& text, int32_t x, int32_t y, int32_t width, bool tail_aligned, bool caret_visible, ants::assets::ColorRGBA color,
                    FontSize size, int32_t screen_w) {
    const int32_t visible = width - renderer.get_text_width("_", size);
    std::string shown = text;
    const int32_t full_w = renderer.get_text_width(shown, size);
    int32_t text_x = x;
    int32_t caret_x = x + full_w;
    if (full_w > visible && tail_aligned) {
        while (!shown.empty() && renderer.get_text_width(shown, size) > visible) shown.erase(0, 1);      // DT_RIGHT: the front is clipped away
        text_x = x + visible - renderer.get_text_width(shown, size);
        caret_x = x + visible;                                                                          // the label stores the clipped width as the text's width
    } else {
        while (!shown.empty() && renderer.get_text_width(shown, size) > visible) shown.pop_back();       // left aligned: clipped at the right edge
    }
    if (!shown.empty()) renderer.draw_text(shown, text_x, y, color, size);
    if (caret_visible && caret_x < screen_w) renderer.draw_text("_", caret_x, y, color, size);
}

void draw_single_line_label(IRenderer& renderer, const std::string& text, int32_t x, int32_t y, int32_t width, bool tail_aligned, ants::assets::ColorRGBA color,
                            FontSize size) {
    std::string shown = text;
    int32_t text_x = x;
    if (renderer.get_text_width(shown, size) > width) {
        if (tail_aligned) {
            while (!shown.empty() && renderer.get_text_width(shown, size) > width) shown.erase(0, 1);      // DT_RIGHT: the front is clipped away
            text_x = x + width - renderer.get_text_width(shown, size);
        } else {
            while (!shown.empty() && renderer.get_text_width(shown, size) > width) shown.pop_back();       // left aligned: clipped at the right edge
        }
    }
    if (!shown.empty()) renderer.draw_text(shown, text_x, y, color, size);
}

}  // namespace ants::app
