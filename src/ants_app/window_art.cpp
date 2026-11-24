// The windows of the match in the new look (window_art.hpp).
#include "ants_app/window_art.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "ants_app/scorecard_layout.hpp"
#include "ants_app/text_layout.hpp"

namespace ants::app::window_art {

namespace {

/// The places that the original's animation `name` covers in its first frame (frame 0 of its first picture); an empty rectangle when the archive has no such animation
LayoutRect original_bounds(const assets::AssetArchive& assets, const char* name) {
    const auto* anim = assets.find_animation(name);
    if (anim == nullptr || anim->subitems.empty() || anim->subitems[0].frames.empty()) return LayoutRect{};
    int32_t x0 = INT32_MAX, y0 = INT32_MAX, x1 = INT32_MIN, y1 = INT32_MIN;
    for (const auto& part : anim->subitems[0].frames) {
        const assets::Sprite& s = assets.get_sprite(part.sprite_index);
        x0 = std::min(x0, static_cast<int32_t>(part.dx));
        y0 = std::min(y0, static_cast<int32_t>(part.dy));
        x1 = std::max(x1, static_cast<int32_t>(part.dx) + static_cast<int32_t>(s.width));
        y1 = std::max(y1, static_cast<int32_t>(part.dy) + static_cast<int32_t>(s.height));
    }
    return LayoutRect{x0, y0, x1 - x0, y1 - y0};
}

/// An animation of the set that replaces the original's `name`: there, and covering the original's place. The original's parts are in numbers relative to an origin that its screen sets (a button's
/// own corner, the dialog's corner (100, 100), or none: the window's own numbers). `assets` without the original's animation (a test archive) leaves nothing to compare with
struct Replaced {
    const char* name;
    const char* base;           // the original's animation whose place the piece must keep: the resting picture of the control (its other pictures are drawn inside or over it)
    int32_t origin_x;
    int32_t origin_y;
};

bool replaces(const hud_art::Art& art, const assets::AssetArchive& assets, const Replaced& what) {
    const LayoutRect mine = hud_art::animation_bounds(art, what.name, 0);
    if (mine.w <= 0) return false;
    const LayoutRect theirs = original_bounds(assets, what.base);
    return theirs.w <= 0 || fits(mine, theirs.moved(what.origin_x, what.origin_y));
}

bool all_replace(const hud_art::Art& art, const assets::AssetArchive& assets, std::initializer_list<Replaced> list) {
    for (const Replaced& what : list) {
        if (!replaces(art, assets, what)) return false;
    }
    return true;
}

bool has_sprites(const hud_art::Art& art, std::initializer_list<const char*> names) {
    for (const char* name : names) {
        if (art.find_sprite(name) < 0) return false;
    }
    return true;
}

}  // namespace

bool fits(const LayoutRect& art_piece, const LayoutRect& original, int32_t shadow) noexcept {
    const auto near_by = [](int32_t a, int32_t b, int32_t below, int32_t above) { return a >= b - below && a <= b + above; };
    return near_by(art_piece.x, original.x, 1, 1) && near_by(art_piece.y, original.y, 1, 1) && near_by(art_piece.right(), original.right(), 1, shadow) && near_by(art_piece.bottom(), original.bottom(), 1, shadow);
}

Pieces check(const hud_art::Art& art, const assets::AssetArchive& assets) {
    Pieces p;
    if (!art.loaded() || !hud_art::has_panel(art)) return p;
    constexpr int32_t kD = ScreenLayout::kDialogX;           // the dialog's corner: the alliance buttons' numbers start there (100, 100)
    p.dialogs = all_replace(art, assets, {{"yes1", "yes1", 180, 260}, {"yes2", "yes1", 180, 260}, {"yes3", "yes1", 180, 260}, {"no1", "no1", 292, 260}, {"no2", "no1", 292, 260}, {"no3", "no1", 292, 260},
                                          {"dyn_byes1", "dyn_byes1", kD, kD}, {"dyn_byes2", "dyn_byes1", kD, kD}, {"dyn_byes3", "dyn_byes1", kD, kD},
                                          {"dyn_bno1", "dyn_bno1", kD, kD}, {"dyn_bno2", "dyn_bno1", kD, kD}, {"dyn_bno3", "dyn_bno1", kD, kD},
                                          {"dad_bacc1", "dad_bacc1", kD, kD}, {"dad_bacc2", "dad_bacc1", kD, kD}, {"dad_bacc3", "dad_bacc1", kD, kD},
                                          {"dad_bdec1", "dad_bdec1", kD, kD}, {"dad_bdec2", "dad_bdec1", kD, kD}, {"dad_bdec3", "dad_bdec1", kD, kD},
                                          {"dw_bwith1", "dw_bwith1", kD, kD}, {"dw_bwith2", "dw_bwith1", kD, kD}, {"dw_bwith3", "dw_bwith1", kD, kD}});
    p.options = art.find_animation("op_window") != nullptr &&
                all_replace(art, assets, {{"breturn1", "breturn1", 0, 0}, {"breturn2", "breturn1", 0, 0}, {"breturn3", "breturn1", 0, 0},
                                          {"op_conu", "op_conu", 0, 0}, {"op_conr", "op_conu", 0, 0}, {"op_cond", "op_conu", 0, 0}, {"op_condr", "op_conu", 0, 0},
                                          {"op_coffu", "op_coffu", 0, 0}, {"op_coffr", "op_coffu", 0, 0}, {"op_coffd", "op_coffu", 0, 0}, {"op_coffdr", "op_coffu", 0, 0},
                                          {"op_honu", "op_honu", 0, 0}, {"op_honr", "op_honu", 0, 0}, {"op_hond", "op_honu", 0, 0}, {"optondr", "op_honu", 0, 0},
                                          {"op_hoffu", "op_hoffu", 0, 0}, {"op_hoffr", "op_hoffu", 0, 0}, {"op_hoffd", "op_hoffu", 0, 0}, {"op_hoffdr", "op_hoffu", 0, 0}}) &&
                art.find_animation("op_conh") != nullptr && art.find_animation("op_coffh") != nullptr && art.find_animation("op_honh") != nullptr && art.find_animation("op_hoffh") != nullptr &&
                has_sprites(art, {"op_thumb_up", "op_thumb_hover", "op_thumb_held"});
    p.quick_help = art.find_animation("qh_window") != nullptr && all_replace(art, assets, {{"qh_return1", "qh_return1", 0, 0}, {"qh_return2", "qh_return1", 0, 0}, {"qh_return3", "qh_return1", 0, 0}}) &&
                   assets.find_sprite_id("qh1.bmp") >= 0 && assets.find_sprite_id("qh2.bmp") >= 0;
    p.countdown = art.find_animation("cd_card") != nullptr && art.find_animation("cd_row") != nullptr &&
                  has_sprites(art, {"cd_dot_0", "cd_dot_1", "cd_dot_2", "cd_dot_3", "dlg_btn_fullscreen_up", "dlg_btn_fullscreen_down"});
    p.fullscreen = has_fullscreen_button(art);
    return p;
}

bool has_results_page(const hud_art::Art& art) {
    if (!art.loaded() || !hud_art::has_panel(art)) return false;
    for (const char* name : scorecard_layout::kSprites) {
        if (art.find_sprite(name) < 0) return false;
    }
    const auto size_is = [&](const char* name, int32_t w, int32_t h) {
        const hud_art::Sprite* s = art.sprite_at(art.find_sprite(name));
        return s != nullptr && s->w == w && s->h == h;
    };
    constexpr int32_t c = scorecard_layout::kWellCorner;
    constexpr int32_t e = scorecard_layout::kBigEnd;
    for (const char* state : {"up", "hover", "down"}) {
        const std::string st = state;
        if (!size_is(("sc_btn_l_" + st).c_str(), e, scorecard_layout::kBigH) || !size_is(("sc_btn_m_" + st).c_str(), scorecard_layout::kBigRun, scorecard_layout::kBigH) ||
            !size_is(("sc_btn_r_" + st).c_str(), e, scorecard_layout::kBigH) || !size_is(("sc_leave_" + st).c_str(), scorecard_layout::kLeaveW, scorecard_layout::kLeaveH) ||
            !size_is(("sc_sm_l_" + st).c_str(), e, scorecard_layout::kSmallH) || !size_is(("sc_sm_m_" + st).c_str(), scorecard_layout::kBigRun, scorecard_layout::kSmallH) ||
            !size_is(("sc_sm_r_" + st).c_str(), e, scorecard_layout::kSmallH) || !size_is(("sc_watch_" + st).c_str(), scorecard_layout::kWatchW, scorecard_layout::kWatchH)) {
            return false;
        }
    }
    return size_is("sc_well_tl", c, c) && size_is("sc_well_tr", c, c) && size_is("sc_well_bl", c, c) && size_is("sc_well_br", c, c) && size_is("sc_well_t", scorecard_layout::kWellRun, c) &&
           size_is("sc_well_b", scorecard_layout::kWellRun, c) && size_is("sc_well_l", c, scorecard_layout::kWellRunY) && size_is("sc_well_r", c, scorecard_layout::kWellRunY);
}

bool has_fullscreen_button(const hud_art::Art& art) noexcept {
    return has_sprites(art, {"button_fullscreen_up", "button_fullscreen_hover", "button_fullscreen_down", "button_fullscreen_leave_up", "button_fullscreen_leave_hover", "button_fullscreen_leave_down"});
}

std::array<LayoutRect, 4> veil_bands(const LayoutRect& picture, const LayoutRect& card) noexcept {
    return {LayoutRect{picture.x, picture.y, picture.w, card.y - picture.y},
            LayoutRect{picture.x, card.y + card.h, picture.w, picture.y + picture.h - (card.y + card.h)},
            LayoutRect{picture.x, card.y, card.x - picture.x, card.h},
            LayoutRect{card.x + card.w, card.y, picture.x + picture.w - (card.x + card.w), card.h}};
}

std::vector<std::string> draw_sentence(IRenderer& renderer, const std::string& text, const SentenceStyle& style, int32_t centre_x, int32_t top) {
    const std::vector<std::string> lines = wrap_label_with([&](const std::string& s) { return renderer.dialog_text_width(s, style.em_tenths); }, text, style.width);
    for (size_t i = 0; i < lines.size(); ++i) {
        const int32_t w = renderer.dialog_text_width(lines[i], style.em_tenths);
        renderer.draw_dialog_text(lines[i], centre_x - w / 2, top + style.first_baseline + static_cast<int32_t>(i) * style.pitch, kInk, style.em_tenths);
    }
    return lines;
}

// ---- the pictures kept from the original's quick help ---------------------------------------------------------------------------------------------------------

namespace {

struct Box {
    const char* name;
    int32_t x0, y0, x1, y1;       // in the window's 640 x 480 numbers
};
// where each picture stands in the original's two bitmaps (qh1 at (10, 9), qh2 at (267, 10)), measured on them
constexpr Box kBoxes[] = {
    {"ant_win", 148, 52, 186, 99},    {"ant_move", 144, 148, 180, 201}, {"ant_food", 66, 300, 98, 353},   {"hill_0", 74, 409, 117, 452},   {"hill_1", 121, 409, 164, 452},
    {"hill_2", 168, 409, 211, 452},   {"hill_3", 215, 409, 258, 452},   {"pu_swim", 279, 133, 318, 169},  {"pu_bomb", 279, 180, 319, 224},  {"pu_thief", 281, 230, 317, 264},
    {"pu_fire", 279, 280, 317, 309},  {"pu_comb", 276, 330, 322, 364},  {"ant_swim", 346, 128, 396, 180}, {"ant_bomb", 346, 180, 394, 227}, {"ant_thief", 344, 226, 392, 272},
    {"ant_fire", 346, 268, 396, 320}, {"ant_comb", 344, 320, 398, 375}, {"bomb_0", 556, 193, 576, 224},   {"bomb_1", 576, 193, 596, 224},   {"bomb_2", 596, 193, 616, 224},
    {"fire", 581, 287, 617, 320},     {"ant_steps", 274, 388, 316, 436},
};

struct Rgb {
    int r, g, b;
};

/// The place of the pixel (x, y) in a picture `w` wide (row after row)
size_t cell(int32_t x, int32_t y, int32_t w) noexcept { return static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x); }
size_t area(int32_t w, int32_t h) noexcept { return static_cast<size_t>(w) * static_cast<size_t>(h); }

bool close_to(const Rgb& a, const Rgb& b, int total) { return std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b) < total; }

}  // namespace

bool KeptPictures::build(const assets::AssetArchive& assets) {
    pictures_.clear();
    const int32_t qh1 = assets.find_sprite_id("qh1.bmp");
    const int32_t qh2 = assets.find_sprite_id("qh2.bmp");
    if (qh1 < 0 || qh2 < 0) return false;
    // the page as the original builds it: the clay, qh1 at (10, 9) and qh2 at (267, 10)
    constexpr int32_t W = 640, H = 480;
    std::vector<Rgb> page(area(W, H), Rgb{219, 75, 19});
    const auto& palette = assets.get_palette();
    const auto lay = [&](int32_t id, int32_t ox, int32_t oy) {
        const assets::Sprite& s = assets.get_sprite(static_cast<uint32_t>(id));
        for (uint32_t y = 0; y < s.height; ++y) {
            for (uint32_t x = 0; x < s.width; ++x) {
                const int32_t px = ox + static_cast<int32_t>(x), py = oy + static_cast<int32_t>(y);
                if (px < 0 || py < 0 || px >= W || py >= H) continue;
                const assets::ColorRGBA& c = palette[s.get_pixel(x, y)];
                page[cell(px, py, W)] = Rgb{c.r, c.g, c.b};
            }
        }
    };
    lay(qh1, 10, 9);
    lay(qh2, 267, 10);
    const Rgb ground{225, 149, 92}, clay{219, 75, 19}, edge{235, 107, 19};
    for (const Box& box : kBoxes) {
        const int32_t bw = box.x1 - box.x0, bh = box.y1 - box.y0;
        const auto at = [&](int32_t x, int32_t y) { return page[cell(box.x0 + x, box.y0 + y, W)]; };
        std::vector<uint8_t> keep(area(bw, bh), 1);
        const bool hill = std::strncmp(box.name, "hill", 4) == 0;
        if (!hill) {
            const bool ant = std::strncmp(box.name, "ant", 3) == 0;
            for (int32_t y = 0; y < bh; ++y) {
                for (int32_t x = 0; x < bw; ++x) {
                    const Rgb c = at(x, y);
                    bool solid = !close_to(c, ground, 4);
                    if (ant && (close_to(c, clay, 2) || close_to(c, edge, 2))) solid = false;       // (an ant stands across a well's edge)
                    keep[cell(x, y, bw)] = solid ? 1 : 0;
                }
            }
            // only the largest connected part stays (no letter that reaches into a box); the ant that carries food keeps every part of any size
            std::vector<int32_t> label(area(bw, bh), 0);
            std::vector<int32_t> sizes{0};
            for (int32_t y0 = 0; y0 < bh; ++y0) {
                for (int32_t x0 = 0; x0 < bw; ++x0) {
                    if (!keep[cell(x0, y0, bw)] || label[cell(x0, y0, bw)]) continue;
                    const int32_t id = static_cast<int32_t>(sizes.size());
                    int32_t count = 0;
                    std::vector<std::pair<int32_t, int32_t>> stack{{y0, x0}};
                    label[cell(x0, y0, bw)] = id;
                    while (!stack.empty()) {
                        const auto [y, x] = stack.back();
                        stack.pop_back();
                        ++count;
                        for (int32_t yy = y - 1; yy <= y + 1; ++yy) {
                            for (int32_t xx = x - 1; xx <= x + 1; ++xx) {
                                if (yy < 0 || xx < 0 || yy >= bh || xx >= bw) continue;
                                const size_t i = cell(xx, yy, bw);
                                if (keep[i] && !label[i]) {
                                    label[i] = id;
                                    stack.push_back({yy, xx});
                                }
                            }
                        }
                    }
                    sizes.push_back(count);
                }
            }
            size_t largest = 0;
            for (size_t i = 1; i < sizes.size(); ++i) {
                if (sizes[i] > sizes[largest]) largest = i;
            }
            const bool food = std::strcmp(box.name, "ant_food") == 0;
            for (size_t i = 0; i < keep.size(); ++i) {
                const size_t l = static_cast<size_t>(label[i]);
                keep[i] = l != 0 && (l == largest || (food && sizes[l] > 20)) ? 1 : 0;
            }
        }
        int32_t cx0 = bw, cy0 = bh, cx1 = 0, cy1 = 0;                 // the part that is left
        for (int32_t y = 0; y < bh; ++y) {
            for (int32_t x = 0; x < bw; ++x) {
                if (!keep[cell(x, y, bw)]) continue;
                cx0 = std::min(cx0, x);
                cy0 = std::min(cy0, y);
                cx1 = std::max(cx1, x + 1);
                cy1 = std::max(cy1, y + 1);
            }
        }
        if (cx1 <= cx0 || cy1 <= cy0) continue;
        Picture pic;
        pic.name = box.name;
        pic.x = box.x0 + cx0;
        pic.y = box.y0 + cy0;
        pic.w = cx1 - cx0;
        pic.h = cy1 - cy0;
        pic.rgba.assign(area(pic.w, pic.h) * 4u, 0);
        for (int32_t y = 0; y < pic.h; ++y) {
            for (int32_t x = 0; x < pic.w; ++x) {
                const Rgb c = at(cx0 + x, cy0 + y);
                uint8_t* dst = &pic.rgba[cell(x, y, pic.w) * 4u];
                dst[0] = static_cast<uint8_t>(c.r);
                dst[1] = static_cast<uint8_t>(c.g);
                dst[2] = static_cast<uint8_t>(c.b);
                dst[3] = keep[cell(cx0 + x, cy0 + y, bw)] ? 255 : 0;
            }
        }
        pictures_.push_back(std::move(pic));
    }
    return !pictures_.empty();
}

void KeptPictures::draw(IRenderer& renderer) const {
    for (const Picture& p : pictures_) renderer.draw_rgba_blended(p.x, p.y, p.w, p.h, p.rgba.data());
}

// ---- the Classic look's corner ------------------------------------------------------------------------------------------------------------------------------------

namespace {

// the picture on the new button: two pixels thick, in an 11 x 11 box; four corner brackets (go fullscreen) and their inward twin (leave)
struct Bar {
    int x, y, w, h;
};
constexpr Bar kEnter[8] = {{0, 0, 4, 2}, {0, 0, 2, 4}, {7, 0, 4, 2}, {9, 0, 2, 4}, {0, 9, 4, 2}, {0, 7, 2, 4}, {7, 9, 4, 2}, {9, 7, 2, 4}};
constexpr Bar kLeave[8] = {{0, 2, 4, 2}, {2, 0, 2, 4}, {7, 2, 4, 2}, {7, 0, 2, 4}, {0, 7, 4, 2}, {2, 7, 2, 4}, {7, 7, 4, 2}, {7, 7, 2, 4}};

struct Picture {
    int32_t w{0}, h{0};
    std::vector<Rgb> px;
    Rgb& at(int32_t x, int32_t y) { return px[cell(x, y, w)]; }
};

bool read_sprite(const assets::AssetArchive& assets, const char* name, Picture& out) {
    const int32_t id = assets.find_sprite_id(name);
    if (id < 0) return false;
    const assets::Sprite& s = assets.get_sprite(static_cast<uint32_t>(id));
    out.w = static_cast<int32_t>(s.width);
    out.h = static_cast<int32_t>(s.height);
    out.px.resize(area(out.w, out.h));
    for (int32_t y = 0; y < out.h; ++y) {
        for (int32_t x = 0; x < out.w; ++x) {
            const assets::ColorRGBA& c = assets.get_palette()[s.get_pixel(static_cast<uint32_t>(x), static_cast<uint32_t>(y))];
            out.at(x, y) = Rgb{c.r, c.g, c.b};
        }
    }
    return true;
}

std::vector<uint8_t> opaque(const Picture& p) {
    std::vector<uint8_t> out(area(p.w, p.h) * 4u);
    for (size_t i = 0; i < p.px.size(); ++i) {
        out[i * 4 + 0] = static_cast<uint8_t>(p.px[i].r);
        out[i * 4 + 1] = static_cast<uint8_t>(p.px[i].g);
        out[i * 4 + 2] = static_cast<uint8_t>(p.px[i].b);
        out[i * 4 + 3] = 255;
    }
    return out;
}

}  // namespace

bool ClassicCorner::build(const assets::AssetArchive& assets) {
    ready_ = false;
    Picture bar, panel, up, down, hover;
    if (!read_sprite(assets, "x0y0.bmp", bar) || !read_sprite(assets, "x458y22.bmp", panel) || !read_sprite(assets, "butquitu.bmp", up) || !read_sprite(assets, "butquitd.bmp", down) ||
        !read_sprite(assets, "butquitr.bmp", hover) || bar.w != 640 || bar.h < 22 || panel.w != 182 || panel.h < 9 || up.w != 46 || up.h != 23 || down.w != 47 || down.h != 23 || hover.w != 25 || hover.h != 13) {
        return false;
    }
    // the plate: the 13 columns right of Quit (the picture's columns 946 .. 958 at 960 wide: the top bar's columns 626 .. 638 above row 22, the panel's top piece's 168 .. 180 below), rows 7 .. 30,
    // laid again and again from the left of the buttons' old places, every second copy mirrored so that no seam shows
    constexpr int32_t kStrip = 13, kRows = 24;
    Picture strip;
    strip.w = kStrip;
    strip.h = kRows;
    strip.px.resize(area(kStrip, kRows));
    for (int32_t y = 0; y < kRows; ++y) {
        for (int32_t x = 0; x < kStrip; ++x) strip.at(x, y) = y + 7 < 22 ? bar.at(626 + x, y + 7) : panel.at(168 + x, y + 7 - 22);
    }
    plate_w_ = 640 - kPlateLeft;
    Picture plate;
    plate.w = plate_w_;
    plate.h = kRows;
    plate.px.resize(area(plate.w, plate.h));
    for (int32_t y = 0; y < kRows; ++y) {
        for (int32_t x = 0; x < plate.w; ++x) {
            const int32_t k = x % (2 * kStrip);
            plate.at(x, y) = strip.at(k < kStrip ? k : 2 * kStrip - 1 - k, y);
        }
    }
    plate_ = opaque(plate);
    // the button: the Quit button's two ends (its first 9 and last 11 columns, where no letter is) with the picture in the letters' own colors
    for (int leave = 0; leave < 2; ++leave) {
        for (int state = 0; state < 3; ++state) {
            Picture b;
            b.w = kButtonW;
            b.h = kButtonH;
            b.px.resize(area(b.w, b.h));
            int32_t ox = 4, oy = 4;
            Rgb ink{251, 251, 255}, shade{0, 0, 0};
            for (int32_t y = 0; y < kButtonH; ++y) {
                for (int32_t x = 0; x < kButtonW; ++x) {
                    if (state == 2) b.at(x, y) = x < 10 ? down.at(x, y) : down.at(37 + (x - 10), y);
                    else b.at(x, y) = x < 9 ? up.at(x, y) : up.at(35 + (x - 9), y);
                }
            }
            if (state == 2) {
                ox = 5;
                oy = 6;
                ink = Rgb{191, 239, 227};
            } else if (state == 1) {
                for (int32_t y = 0; y < 13; ++y) {
                    for (int32_t x = 3; x < 17; ++x) b.at(x, 3 + y) = hover.at(0, y);          // the original's hover lays a lighter label on the face: its ground rows
                }
                shade = Rgb{19, 43, 27};
            }
            const Bar* bars = leave != 0 ? kLeave : kEnter;
            for (int layer = 0; layer < 2; ++layer) {
                const int32_t d = layer == 0 ? 1 : 0;
                const Rgb col = layer == 0 ? shade : ink;
                for (int i = 0; i < 8; ++i) {
                    for (int32_t y = 0; y < bars[i].h; ++y) {
                        for (int32_t x = 0; x < bars[i].w; ++x) b.at(ox + bars[i].x + d + x, oy + bars[i].y + d + y) = col;
                    }
                }
            }
            buttons_[static_cast<size_t>(leave) * 3 + static_cast<size_t>(state)] = opaque(b);
        }
    }
    ready_ = true;
    return true;
}

}  // namespace ants::app::window_art
