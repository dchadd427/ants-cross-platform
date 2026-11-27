#include "ants_app/text_layout.hpp"

#include <algorithm>
#include <cstddef>

namespace ants::app {

std::string::size_type utf8_char_bytes(const std::string& text, std::string::size_type at) {
    if (at >= text.size()) return 0;
    const unsigned char lead = static_cast<unsigned char>(text[at]);
    std::string::size_type n = lead < 0x80 ? 1 : lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;      // (a byte that starts nothing is one by itself)
    if (at + n > text.size()) n = text.size() - at;
    for (std::string::size_type i = 1; i < n; ++i) {
        if ((static_cast<unsigned char>(text[at + i]) & 0xC0) != 0x80) return i;                                 // (a sequence that is cut is as long as it goes)
    }
    return n;
}

void pop_back_char(std::string& text) {
    if (text.empty()) return;
    std::string::size_type at = text.size() - 1;
    while (at > 0 && (static_cast<unsigned char>(text[at]) & 0xC0) == 0x80 && text.size() - at < 4) --at;       // back to the start of the last character
    text.erase(at);
}

void erase_front_char(std::string& text) {
    if (!text.empty()) text.erase(0, utf8_char_bytes(text, 0));
}

}  // namespace ants::app

namespace ants::app {

namespace {

// One paragraph (no '\n' inside), the loop of FUN_0102b0b5. The scan goes a character at a time, and a character is a whole UTF-8 sequence (a name may hold letters of two or three bytes, protocol 29):
// for plain ASCII every step is one byte, as it was.
void wrap_paragraph(const WidthFn& width_of, const std::string& text, int32_t width, std::vector<std::string>& lines) {
    const size_t len = text.size();
    size_t p = 0;
    while (p < len) {
        while (p < len && text[p] == ' ') ++p;                     // leading spaces of a line are skipped
        if (p >= len) break;
        size_t last_word_end = 0;                                  // bytes from the line start to the end of the last word seen: 0 = none
        int32_t last_word_index = 0;                               // the index (from the line start) of that word's last character
        int32_t count = 0;                                         // characters scanned so far
        size_t q = p;
        while (q < len) {
            const size_t step = utf8_char_bytes(text, q);
            if (width_of(text.substr(p, q + step - p)) >= width) break;      // the text so far has reached the width
            if (text[q] != ' ' && (q + step >= len || text[q + step] == ' ')) {
                last_word_end = q + step - p;
                last_word_index = count;
            }
            ++count;
            q += step;
        }
        size_t n = last_word_index > 0 ? last_word_end : q - p;     // no word end seen (or only the first character's): cut one character before the width was reached
        if (n == 0) n = utf8_char_bytes(text, p);                   // (the original would loop forever on an empty cut)
        lines.push_back(text.substr(p, n));
        p += n;
    }
}

}  // namespace

std::vector<std::string> wrap_label_with(const WidthFn& width_of, const std::string& text, int32_t width) {
    std::vector<std::string> lines;
    if (width <= 0) {
        if (!text.empty()) lines.push_back(text);
        return lines;
    }
    size_t start = 0;
    while (start <= text.size()) {
        const size_t next = text.find('\n', start);
        wrap_paragraph(width_of, text.substr(start, next == std::string::npos ? std::string::npos : next - start), width, lines);
        if (next == std::string::npos) break;
        start = next + 1;
    }
    return lines;
}

std::vector<std::string> wrap_label_text(const IRenderer& renderer, const std::string& text, int32_t width, FontSize size) {
    return wrap_label_with([&](const std::string& line) { return renderer.get_text_width(line, size); }, text, width);
}

std::string fit_text(const IRenderer& renderer, std::string text, int32_t width, FontSize size) {
    while (utf8_char_bytes(text, 0) < text.size() && renderer.get_text_width(text, size) > width) pop_back_char(text);       // (a character is a whole UTF-8 sequence; one character stays)
    return text;
}

std::string fit_name(const IRenderer& renderer, std::string text, int32_t width, FontSize size) {
    if (renderer.get_text_width(text, size) <= width) return text;
    static const std::string kDots = "...";
    while (!text.empty() && renderer.get_text_width(text + kDots, size) > width) pop_back_char(text);
    while (!text.empty() && text.back() == ' ') text.pop_back();
    return text + kDots;
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
    while (!last.empty() && renderer.get_text_width(last + kDots, size) > width) pop_back_char(last);       // (what is left of the line, and the dots behind it, fit the box)
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
        while (!shown.empty() && renderer.get_text_width(shown, size) > visible) erase_front_char(shown);      // DT_RIGHT: the front is clipped away
        text_x = x + visible - renderer.get_text_width(shown, size);
        caret_x = x + visible;                                                                          // the label stores the clipped width as the text's width
    } else {
        while (!shown.empty() && renderer.get_text_width(shown, size) > visible) pop_back_char(shown);       // left aligned: clipped at the right edge
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
            while (!shown.empty() && renderer.get_text_width(shown, size) > width) erase_front_char(shown);      // DT_RIGHT: the front is clipped away
            text_x = x + width - renderer.get_text_width(shown, size);
        } else {
            while (!shown.empty() && renderer.get_text_width(shown, size) > width) pop_back_char(shown);       // left aligned: clipped at the right edge
        }
    }
    if (!shown.empty()) renderer.draw_text(shown, text_x, y, color, size);
}

}  // namespace ants::app
