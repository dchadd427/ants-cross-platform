// The HUD art (hud_art.hpp): the manifest reader, where each piece stands in a picture of any shape, and the drawing through the renderer.
#include "ants_app/hud_art.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <iterator>
#include <sstream>

#include "ants_app/renderer.hpp"

namespace ants::app::hud_art {

namespace {

bool to_int(const std::string& text, int32_t& out) {
    if (text.empty() || text.size() > 9) return false;
    char* end = nullptr;
    const long v = std::strtol(text.c_str(), &end, 10);
    if (end == nullptr || *end != '\0') return false;
    out = static_cast<int32_t>(v);
    return true;
}

std::vector<std::string> words_of(const std::string& line) {
    std::istringstream in(line);
    std::vector<std::string> out;
    std::string w;
    while (in >> w) out.push_back(w);
    return out;
}

/// The whole file as bytes. Never throws: a file that cannot be read, and a folder where the file should be (the standard library of Linux throws when it reads one), are a failure
bool read_file(const std::string& path, std::string& out) {
    try {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        out.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        return !f.bad();
    } catch (const std::exception&) {
        return false;
    }
}

}  // namespace

int32_t Art::scale_for(double output_scale) noexcept {
    if (!(output_scale > 1.0 + 1e-6)) return 1;           // (also a number that is not one)
    if (output_scale <= 2.0 + 1e-6) return 2;
    return 3;
}

int32_t Art::find_sprite(const std::string& name) const noexcept {
    const auto it = sprite_index_.find(name);
    return it == sprite_index_.end() ? -1 : it->second;
}

const Animation* Art::find_animation(const std::string& name) const noexcept {
    const auto it = animation_index_.find(name);
    return it == animation_index_.end() ? nullptr : &animations_[it->second];
}

const std::string& Art::sheet_file(int32_t scale, int32_t sheet) const {
    static const std::string none;
    if (scale < 1 || scale > 3 || sheet < 0 || sheet > 1) return none;
    return sheet == 0 ? sheets_[static_cast<size_t>(scale - 1)] : sheets2_[static_cast<size_t>(scale - 1)];
}

bool Art::parse(const std::string& manifest) {
    *this = Art{};
    const auto fail = [this](const std::string& why) {
        what_ = why;
        sprites_.clear();
        animations_.clear();
        sprite_index_.clear();
        animation_index_.clear();
        loaded_ = false;
        return false;
    };
    std::istringstream in(manifest);
    std::string line;
    bool header = false;
    Animation* current = nullptr;
    int32_t canvas_w = 0;
    int32_t canvas_h = 0;
    size_t line_no = 0;
    while (std::getline(in, line)) {
        ++line_no;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::vector<std::string> w = words_of(line);
        if (w.empty()) continue;
        const std::string where = "line " + std::to_string(line_no) + ": ";
        if (!header) {
            if (w.size() != 2 || w[0] != "ants-hud-art" || w[1] != "1") return fail("not a HUD art manifest of version 1");
            header = true;
        } else if (w[0] == "canvas") {
            if (w.size() != 3 || !to_int(w[1], canvas_w) || !to_int(w[2], canvas_h) || canvas_w != kReferenceWidth || canvas_h != kReferenceHeight) return fail(where + "the canvas is not 960 x 540");
        } else if (w[0] == "sheetsize") {
            if (w.size() != 3 || !to_int(w[1], sheet_w_) || !to_int(w[2], sheet_h_) || sheet_w_ <= 0 || sheet_h_ <= 0) return fail(where + "a bad sheet size");
        } else if (w[0] == "sheet2size") {
            if (w.size() != 3 || !to_int(w[1], sheet2_w_) || !to_int(w[2], sheet2_h_) || sheet2_w_ <= 0 || sheet2_h_ <= 0) return fail(where + "a bad second sheet size");
        } else if (w[0] == "derive2x") {
            if (w.size() != 2 || !to_int(w[1], derived_rows_) || derived_rows_ <= 0) return fail(where + "a bad derive2x record");
        } else if (w[0] == "sheet") {
            int32_t scale = 0;
            if (w.size() != 3 || !to_int(w[1], scale) || scale < 1 || scale > 3 || w[2].find('/') != std::string::npos || w[2].find('\\') != std::string::npos || w[2].find("..") != std::string::npos) return fail(where + "a bad sheet record");
            sheets_[static_cast<size_t>(scale - 1)] = w[2];
        } else if (w[0] == "sheet2") {
            int32_t scale = 0;
            if (w.size() != 3 || !to_int(w[1], scale) || scale < 1 || scale > 3 || w[2].find('/') != std::string::npos || w[2].find('\\') != std::string::npos || w[2].find("..") != std::string::npos) return fail(where + "a bad second sheet record");
            sheets2_[static_cast<size_t>(scale - 1)] = w[2];
        } else if (w[0] == "sprite" || w[0] == "sprite2") {
            if (w.size() != 10 && w.size() != 15) return fail(where + "a sprite record has 9 or 14 fields");
            Sprite s;
            s.name = w[1];
            s.sheet = w[0] == "sprite2" ? 1 : 0;
            if (!to_int(w[2], s.w) || !to_int(w[3], s.h) || !to_int(w[4], s.sheet_x) || !to_int(w[5], s.sheet_y) || !to_int(w[6], s.x) || !to_int(w[7], s.y)) return fail(where + "a number is not one");
            if (s.w <= 0 || s.h <= 0 || s.sheet_x < 0 || s.sheet_y < 0) return fail(where + "a sprite without a size or place");
            if (w[8] == "left") s.anchor_x = AnchorX::Left;
            else if (w[8] == "right") s.anchor_x = AnchorX::Right;
            else if (w[8] == "stretch") s.anchor_x = AnchorX::Stretch;
            else return fail(where + "an anchor that is none of left, right, stretch");
            if (w[9] == "top") s.anchor_y = AnchorY::Top;
            else if (w[9] == "bottom") s.anchor_y = AnchorY::Bottom;
            else if (w[9] == "stretch") s.anchor_y = AnchorY::Stretch;
            else return fail(where + "an anchor that is none of top, bottom, stretch");
            if (w.size() == 15) {
                if (w[10] == "x") s.slice.axis = Axis::X;
                else if (w[10] == "y") s.slice.axis = Axis::Y;
                else return fail(where + "a slice axis that is neither x nor y");
                if (!to_int(w[11], s.slice.start) || !to_int(w[12], s.slice.tile_from) || !to_int(w[13], s.slice.tile_to) || !to_int(w[14], s.slice.end)) return fail(where + "a slice number is not one");
                const int32_t full = s.slice.axis == Axis::X ? s.w : s.h;
                if (s.slice.start < 0 || s.slice.end < 0 || s.slice.tile_from < 0 || s.slice.tile_to <= s.slice.tile_from || s.slice.tile_to > full || s.slice.start + s.slice.end > full) return fail(where + "a slice outside its sprite");
            }
            if (sprite_index_.count(s.name) != 0) return fail(where + "the sprite " + s.name + " twice");
            sprite_index_[s.name] = static_cast<int32_t>(sprites_.size());
            sprites_.push_back(std::move(s));
        } else if (w[0] == "anim") {
            if ((w.size() != 2 && !(w.size() == 3 && w[2] == "cursor")) || animation_index_.count(w[1]) != 0) return fail(where + "an animation record that is bad or twice");
            animation_index_[w[1]] = animations_.size();
            animations_.push_back(Animation{w[1], w.size() == 3, {}});
            current = &animations_.back();
        } else if (w[0] == "frame") {
            if (current == nullptr || w.size() < 5 || (w.size() - 2) % 3 != 0) return fail(where + "a frame that is bad or has no animation");
            Frame f;
            if (!to_int(w[1], f.ms) || f.ms <= 0) return fail(where + "a frame without a time");
            for (size_t i = 2; i + 2 < w.size(); i += 3) {
                Part p;
                const auto it = sprite_index_.find(w[i]);
                if (it == sprite_index_.end() || !to_int(w[i + 1], p.x) || !to_int(w[i + 2], p.y)) return fail(where + "a part that names no sprite, or has a bad place");
                p.sprite = it->second;
                f.parts.push_back(p);
            }
            current->frames.push_back(std::move(f));
        } else {
            return fail(where + "a record that is not known: " + w[0]);
        }
    }
    if (!header) return fail("empty");
    if (sheet_w_ <= 0 || sheet_h_ <= 0) return fail("no sheet size");
    for (int32_t scale : kScales) {
        if (sheets_[static_cast<size_t>(scale - 1)].empty()) return fail("no sheet for the scale " + std::to_string(scale));
    }
    if (derived_rows_ > sheet_h_) return fail("derive2x names more rows than the sheet has");
    const bool second = sheet2_w_ > 0 || sheet2_h_ > 0 || !sheets2_[0].empty() || !sheets2_[1].empty() || !sheets2_[2].empty();
    if (second && (sheet2_w_ <= 0 || sheet2_h_ <= 0 || sheets2_[0].empty() || sheets2_[1].empty() || sheets2_[2].empty())) return fail("a second sheet needs its size and its three files");
    for (const Sprite& s : sprites_) {
        if (s.sheet == 1 && !second) return fail("the sprite " + s.name + " is on a second sheet that the manifest does not have");
        if (s.sheet_x + s.w > sheet_width(s.sheet) || s.sheet_y + s.h > sheet_height(s.sheet)) return fail("the sprite " + s.name + " is outside its sheet");
    }
    for (const Animation& a : animations_) {
        if (a.frames.empty()) return fail("the animation " + a.name + " has no frame");
    }
    const struct { int32_t* index; const char* name; bool required; } pieces[] = {
        {&top_bar, "top_bar", true}, {&left_strip, "left_strip", true}, {&bottom_strip, "bottom_strip", true}, {&score_window, "score_window", false},
        {&lower_panel, "lower_panel", true}, {&panel_top, "panel_top", true}, {&minimap_frame, "minimap_frame", true}, {&card, "card", true}, {&status_box, "status_box", true},
    };
    for (const auto& piece : pieces) {
        *piece.index = find_sprite(piece.name);
        if (*piece.index < 0 && piece.required) return fail(std::string("the frame's piece ") + piece.name + " is missing");
    }
    for (const int32_t stretched : {top_bar, left_strip, bottom_strip, lower_panel}) {
        if (sprites_[static_cast<size_t>(stretched)].slice.axis == Axis::None) return fail("a long piece of the frame has no slice");
    }
    static uint64_t counter = 0;
    generation_ = ++counter;
    loaded_ = true;
    return true;
}

bool Art::load(const std::string& folder) {
    std::string dir = folder;
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') dir += '/';
    std::string text;
    if (!read_file(dir + "hud_art.txt", text)) {
        *this = Art{};
        what_ = "no " + dir + "hud_art.txt";
        return false;
    }
    if (!parse(text)) return false;
    folder_ = dir;
    // The sheets are decoded later (when a texture needs one), but they must be there and be the size that the manifest's places assume: the header of each file is read now, so that a folder that lacks
    // a sheet, or has another one, is the original's art from the start and not a frame that stays empty
    for (int32_t which = 0; which <= (has_second_sheet() ? 1 : 0); ++which) {
        for (const int32_t scale : kScales) {
            const std::string& file = sheet_file(scale, which);
            const int32_t wide = sheet_width(which);
            const int32_t high = sheet_height(which);
            std::ifstream sheet(dir + file, std::ios::binary);
            uint8_t head[33] = {};
            sheet.read(reinterpret_cast<char*>(head), sizeof(head));
            const bool png = sheet.gcount() == static_cast<std::streamsize>(sizeof(head)) && std::memcmp(head, "\x89PNG\r\n\x1a\n", 8) == 0 && std::memcmp(head + 12, "IHDR", 4) == 0;
            const auto be32 = [&head](size_t at) { return (static_cast<int64_t>(head[at]) << 24) | (static_cast<int64_t>(head[at + 1]) << 16) | (static_cast<int64_t>(head[at + 2]) << 8) | head[at + 3]; };
            if (!png || be32(16) != static_cast<int64_t>(wide) * scale || be32(20) != static_cast<int64_t>(high) * scale || head[24] != 8 || head[25] != 6) {
                const std::string why = "the sheet " + dir + file + " is missing, or is not a " + std::to_string(wide * scale) + " x " + std::to_string(high * scale) + " picture of 8-bit RGBA";
                *this = Art{};
                what_ = why;
                return false;
            }
        }
    }
    return true;
}

bool Art::sheet_pixels(int32_t scale, DecodedImage& out, int32_t sheet) const {
    out = DecodedImage{};
    const std::string& file = sheet_file(scale, sheet);
    if (!loaded_ || file.empty()) return false;
    DecodedImage derived;
    if (sheet == 0 && scale == 2 && derived_rows_ > 0) {
        // the first rows are made from the 3x sheet, of which only those rows are decoded; they are made first, so that the 3x rows are gone again before the 2x sheet is decoded
        std::string bytes3;
        DecodedImage three;
        if (!read_file(folder_ + sheet_file(3), bytes3) || !decode_png_rgba(reinterpret_cast<const uint8_t*>(bytes3.data()), bytes3.size(), three, derived_rows_ * 3) ||
            three.width != sheet_w_ * 3 || !derive_two_thirds(three, derived) || derived.width != sheet_w_ * 2 || derived.height != derived_rows_ * 2) {
            return false;
        }
    }
    std::string bytes;
    if (!read_file(folder_ + file, bytes)) return false;
    if (!decode_png_rgba(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), out)) return false;
    if (out.width != sheet_width(sheet) * scale || out.height != sheet_height(sheet) * scale) {
        out = DecodedImage{};
        return false;
    }
    std::copy(derived.rgba.begin(), derived.rgba.end(), out.rgba.begin());
    return true;
}

bool derive_two_thirds(const DecodedImage& three, DecodedImage& two) {
    two = DecodedImage{};
    if (three.width <= 0 || three.height <= 0 || three.width % 3 != 0 || three.height % 3 != 0 || three.rgba.size() != static_cast<size_t>(three.width) * static_cast<size_t>(three.height) * 4u) return false;
    static constexpr int32_t kWeight[2][3] = {{2, 1, 0}, {0, 1, 2}};
    const int32_t blocks_x = three.width / 3;
    const int32_t blocks_y = three.height / 3;
    two.width = blocks_x * 2;
    two.height = blocks_y * 2;
    two.rgba.assign(static_cast<size_t>(two.width) * static_cast<size_t>(two.height) * 4u, 0);
    const size_t stride3 = static_cast<size_t>(three.width) * 4u;
    const size_t stride2 = static_cast<size_t>(two.width) * 4u;
    for (int32_t by = 0; by < blocks_y; ++by) {
        for (int32_t bx = 0; bx < blocks_x; ++bx) {
            const uint8_t* block = three.rgba.data() + static_cast<size_t>(by) * 3u * stride3 + static_cast<size_t>(bx) * 12u;
            for (int32_t oy = 0; oy < 2; ++oy) {
                for (int32_t ox = 0; ox < 2; ++ox) {
                    uint32_t cover = 0;                 // the sum of weight * alpha (at most 9 * 255)
                    uint32_t sum[3] = {0, 0, 0};        // the sums of weight * alpha * color
                    for (int32_t sy = 0; sy < 3; ++sy) {
                        for (int32_t sx = 0; sx < 3; ++sx) {
                            const uint32_t weight = static_cast<uint32_t>(kWeight[oy][sy] * kWeight[ox][sx]);
                            if (weight == 0) continue;
                            const uint8_t* px = block + static_cast<size_t>(sy) * stride3 + static_cast<size_t>(sx) * 4u;
                            const uint32_t wa = weight * px[3];
                            cover += wa;
                            for (size_t c = 0; c < 3; ++c) sum[c] += wa * px[c];
                        }
                    }
                    uint8_t* dst = two.rgba.data() + (static_cast<size_t>(by) * 2u + static_cast<size_t>(oy)) * stride2 + (static_cast<size_t>(bx) * 2u + static_cast<size_t>(ox)) * 4u;
                    if (cover > 0) {
                        for (size_t c = 0; c < 3; ++c) dst[c] = static_cast<uint8_t>((2u * sum[c] + cover) / (2u * cover));
                    }
                    dst[3] = static_cast<uint8_t>((2u * cover + 9u) / 18u);
                }
            }
        }
    }
    return true;
}

LayoutPoint place_of(const Sprite& sprite, int32_t x, int32_t y, const ScreenLayout& layout) noexcept {
    return LayoutPoint{x + (sprite.anchor_x == AnchorX::Right ? layout.width - Art::kReferenceWidth : 0), y + (sprite.anchor_y == AnchorY::Bottom ? layout.height - Art::kReferenceHeight : 0)};
}

std::vector<Draw> sliced_draws(const Art& art, int32_t index, int32_t x, int32_t y, int32_t length) {
    std::vector<Draw> out;
    const Sprite* s = art.sprite_at(index);
    if (s == nullptr) return out;
    if (s->slice.axis == Axis::None) {
        out.push_back(Draw{index, LayoutRect{x, y, s->w, s->h}, LayoutRect{0, 0, s->w, s->h}});
        return out;
    }
    const Slice& sl = s->slice;
    const bool horizontal = sl.axis == Axis::X;
    const int32_t full = horizontal ? s->w : s->h;
    const auto piece = [&](int32_t from, int32_t n, int32_t at) {
        if (n <= 0) return;
        if (horizontal) out.push_back(Draw{index, LayoutRect{x + at, y, n, s->h}, LayoutRect{from, 0, n, s->h}});
        else out.push_back(Draw{index, LayoutRect{x, y + at, s->w, n}, LayoutRect{0, from, s->w, n}});
    };
    piece(0, std::min(sl.start, length), 0);
    const int32_t tile = sl.tile_to - sl.tile_from;
    for (int32_t pos = sl.start; tile > 0 && pos < length - sl.end;) {
        const int32_t n = std::min(tile, length - sl.end - pos);
        piece(sl.tile_from, n, pos);
        pos += n;
    }
    const int32_t end = std::min(sl.end, length);
    piece(full - end, end, length - end);
    return out;
}

std::vector<Draw> frame_draws(const Art& art, const ScreenLayout& layout) {
    std::vector<Draw> out;
    const auto add = [&out](std::vector<Draw> more) { out.insert(out.end(), more.begin(), more.end()); };
    const auto at = [&](int32_t index) {
        const Sprite& s = art.sprites()[static_cast<size_t>(index)];
        return place_of(s, s.x, s.y, layout);
    };
    const int32_t dw = layout.width - Art::kReferenceWidth;
    const int32_t dh = layout.height - Art::kReferenceHeight;
    // The long pieces grow with the picture: their length at 960 x 540 (the top bar and the bottom strip reach the right edge, the left strip the bottom edge, the lower panel the bottom strip)
    // plus what the picture has more
    LayoutPoint p = at(art.top_bar);
    add(sliced_draws(art, art.top_bar, p.x, p.y, art.sprites()[static_cast<size_t>(art.top_bar)].w + dw));
    p = at(art.left_strip);
    add(sliced_draws(art, art.left_strip, p.x, p.y, Art::kReferenceHeight - art.sprites()[static_cast<size_t>(art.left_strip)].y + dh));
    p = at(art.bottom_strip);
    add(sliced_draws(art, art.bottom_strip, p.x, p.y, art.sprites()[static_cast<size_t>(art.bottom_strip)].w + dw));
    p = at(art.lower_panel);
    add(sliced_draws(art, art.lower_panel, p.x, p.y, Art::kReferenceHeight - art.sprites()[static_cast<size_t>(art.lower_panel)].y - art.sprites()[static_cast<size_t>(art.bottom_strip)].h + dh));
    for (const int32_t piece : {art.panel_top, art.minimap_frame, art.card, art.status_box}) {
        p = at(piece);
        const Sprite& s = art.sprites()[static_cast<size_t>(piece)];
        out.push_back(Draw{piece, LayoutRect{p.x, p.y, s.w, s.h}, LayoutRect{0, 0, s.w, s.h}});
    }
    return out;
}

std::vector<Draw> frame_part_draws(const Art& art, const Frame& frame, const ScreenLayout& layout) {
    std::vector<Draw> out;
    for (const Part& part : frame.parts) {
        const Sprite* s = art.sprite_at(part.sprite);
        if (s == nullptr) continue;
        const LayoutPoint p = place_of(*s, part.x, part.y, layout);
        out.push_back(Draw{part.sprite, LayoutRect{p.x, p.y, s->w, s->h}, LayoutRect{0, 0, s->w, s->h}});
    }
    return out;
}

std::vector<Draw> animation_draws(const Art& art, const std::string& name, size_t frame, const ScreenLayout& layout) {
    const Animation* a = art.find_animation(name);
    if (a == nullptr || frame >= a->frames.size()) return {};
    return frame_part_draws(art, a->frames[frame], layout);
}

std::vector<Draw> animation_draws_at(const Art& art, const std::string& name, size_t frame, int32_t dx, int32_t dy) {
    std::vector<Draw> out;
    const Animation* a = art.find_animation(name);
    if (a == nullptr || frame >= a->frames.size()) return out;
    for (const Part& part : a->frames[frame].parts) {
        const Sprite* s = art.sprite_at(part.sprite);
        if (s == nullptr) continue;
        out.push_back(Draw{part.sprite, LayoutRect{part.x + dx, part.y + dy, s->w, s->h}, LayoutRect{0, 0, s->w, s->h}});
    }
    return out;
}

LayoutRect animation_bounds(const Art& art, const std::string& name, size_t frame) {
    const Animation* a = art.find_animation(name);
    if (a == nullptr || frame >= a->frames.size() || a->frames[frame].parts.empty()) return LayoutRect{};
    int32_t x0 = INT32_MAX, y0 = INT32_MAX, x1 = INT32_MIN, y1 = INT32_MIN;
    for (const Part& part : a->frames[frame].parts) {
        const Sprite* s = art.sprite_at(part.sprite);
        if (s == nullptr) continue;
        x0 = std::min(x0, part.x);
        y0 = std::min(y0, part.y);
        x1 = std::max(x1, part.x + s->w);
        y1 = std::max(y1, part.y + s->h);
    }
    return x1 > x0 && y1 > y0 ? LayoutRect{x0, y0, x1 - x0, y1 - y0} : LayoutRect{};
}

namespace {
constexpr const char* kPanelNames[9] = {"panel_tl", "panel_t", "panel_tr", "panel_l", "panel_c", "panel_r", "panel_bl", "panel_b", "panel_br"};
}

bool has_panel(const Art& art) noexcept {
    for (const char* name : kPanelNames) {
        if (art.find_sprite(name) < 0) return false;
    }
    return true;
}

std::vector<Draw> panel_draws(const Art& art, const LayoutRect& rect) {
    std::vector<Draw> out;
    if (!has_panel(art)) return out;
    int32_t id[9];
    const Sprite* sp[9];
    for (size_t i = 0; i < 9; ++i) {
        id[i] = art.find_sprite(kPanelNames[i]);
        sp[i] = art.sprite_at(id[i]);
    }
    const int32_t cw = sp[0]->w;                 // the corner's size: the frame's overlay pieces all start from it
    const int32_t ch = sp[0]->h;
    const Sprite& centre = *sp[4];
    if (rect.w < 2 * cw || rect.h < 2 * ch || centre.w <= 0 || centre.h <= 0) return out;
    // the clay: the centre tile over the whole rectangle (the frame lies over its edge), the last copy of each row and column cut
    for (int32_t y = 0; y < rect.h; y += centre.h) {
        const int32_t h = std::min(centre.h, rect.h - y);
        for (int32_t x = 0; x < rect.w; x += centre.w) {
            const int32_t w = std::min(centre.w, rect.w - x);
            out.push_back(Draw{id[4], LayoutRect{rect.x + x, rect.y + y, w, h}, LayoutRect{0, 0, w, h}});
        }
    }
    const auto along_x = [&](int32_t piece, int32_t y) {          // an edge piece repeated between the two corners
        const Sprite& s = *sp[piece];
        for (int32_t x = cw; x < rect.w - cw; x += s.w) {
            const int32_t w = std::min(s.w, rect.w - cw - x);
            out.push_back(Draw{id[piece], LayoutRect{rect.x + x, rect.y + y, w, s.h}, LayoutRect{0, 0, w, s.h}});
        }
    };
    const auto along_y = [&](int32_t piece, int32_t x) {
        const Sprite& s = *sp[piece];
        for (int32_t y = ch; y < rect.h - ch; y += s.h) {
            const int32_t h = std::min(s.h, rect.h - ch - y);
            out.push_back(Draw{id[piece], LayoutRect{rect.x + x, rect.y + y, s.w, h}, LayoutRect{0, 0, s.w, h}});
        }
    };
    along_x(1, 0);
    along_x(7, rect.h - sp[7]->h);
    along_y(3, 0);
    along_y(5, rect.w - sp[5]->w);
    const auto corner = [&](int32_t piece, int32_t x, int32_t y) { out.push_back(Draw{id[piece], LayoutRect{rect.x + x, rect.y + y, sp[piece]->w, sp[piece]->h}, LayoutRect{0, 0, sp[piece]->w, sp[piece]->h}}); };
    corner(0, 0, 0);
    corner(2, rect.w - sp[2]->w, 0);
    corner(6, 0, rect.h - sp[6]->h);
    corner(8, rect.w - sp[8]->w, rect.h - sp[8]->h);
    return out;
}

std::vector<Draw> cursor_draws(const Art& art, const Animation& cursor, size_t frame, int32_t x, int32_t y) {
    std::vector<Draw> out;
    if (frame >= cursor.frames.size()) return out;
    for (const Part& part : cursor.frames[frame].parts) {
        const Sprite* s = art.sprite_at(part.sprite);
        if (s == nullptr) continue;
        out.push_back(Draw{part.sprite, LayoutRect{x + part.x, y + part.y, s->w, s->h}, LayoutRect{0, 0, s->w, s->h}});
    }
    return out;
}

size_t frame_at(const Animation& animation, uint32_t ms) noexcept {
    if (animation.frames.empty()) return 0;
    uint64_t total = 0;
    for (const Frame& f : animation.frames) total += static_cast<uint64_t>(f.ms > 0 ? f.ms : 1);
    uint64_t now = ms % total;
    for (size_t i = 0; i < animation.frames.size(); ++i) {
        const uint64_t duration = static_cast<uint64_t>(animation.frames[i].ms > 0 ? animation.frames[i].ms : 1);
        if (now < duration) return i;
        now -= duration;
    }
    return animation.frames.size() - 1;
}

bool cursor_fits(const Art& art, const Animation& art_cursor, const std::vector<OriginalCursorFrame>& original) noexcept {
    if (!art_cursor.cursor || art_cursor.frames.size() != original.size() || original.empty()) return false;
    for (size_t i = 0; i < original.size(); ++i) {
        const Frame& f = art_cursor.frames[i];
        if (f.ms != original[i].ms || f.parts.size() != 1) return false;
        const Part& part = f.parts[0];
        const Sprite* s = art.sprite_at(part.sprite);
        if (s == nullptr || part.x != original[i].dx || part.y != original[i].dy || s->w != original[i].w || s->h != original[i].h) return false;
    }
    return true;
}

void draw_all(IRenderer& renderer, const Art& art, const std::vector<Draw>& draws) {
    for (const Draw& d : draws) renderer.draw_art(art, d.sprite, d.dst.x, d.dst.y, d.dst.w, d.dst.h, d.src.x, d.src.y, d.src.w, d.src.h);
}

void draw_frame(IRenderer& renderer, const Art& art, const ScreenLayout& layout) {
    draw_all(renderer, art, frame_draws(art, layout));
}

bool draw_animation(IRenderer& renderer, const Art& art, const std::string& name, size_t frame, const ScreenLayout& layout) {
    const Animation* a = art.find_animation(name);
    if (a == nullptr || a->frames.empty()) return false;
    draw_all(renderer, art, frame_part_draws(art, a->frames[std::min(frame, a->frames.size() - 1)], layout));
    return true;
}

}  // namespace ants::app::hud_art
