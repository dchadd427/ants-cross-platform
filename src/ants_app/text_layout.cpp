#include "ants_app/text_layout.hpp"

#include <algorithm>

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

}  // namespace ants::app
