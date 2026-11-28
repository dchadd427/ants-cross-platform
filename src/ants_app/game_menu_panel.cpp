// The settings menu of the desktop game (game_menu_panel.hpp): one walk over the panel that draws it and lists its places, the input, and the rules of what a press does.
#include "ants_app/game_menu_panel.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

#include "ants_app/unit_pen.hpp"
#include "ants_app/window_art.hpp"
#include "ants_sim/game_strings.hpp"

namespace ants::app {

namespace {

using assets::ColorRGBA;

// The panel's numbers, in units (the panel is 720 x 484): the inside of its frame, the tabs, the well that holds the rows, the foot
constexpr int32_t kInX = 44, kInY = 38, kInR = kMenuPanelW - 44, kInB = kMenuPanelH - 36;
constexpr int32_t kTabW = 118, kTabH = 26, kTabPitch = 34, kTabTop = kInY + 52;
constexpr int32_t kWellX = kInX + 140, kWellY = kInY + 52, kWellW = kInR - kWellX, kWellH = 306;
constexpr int32_t kRowH = 46, kRowPad = 14, kHintLine = 12;
constexpr int32_t kSwitchW = 100, kSliderW = 160, kButtonH = 26, kEditH = 22;
constexpr int32_t kFootY = kInB - 26;

const ColorRGBA kCream{244, 236, 226, 255};
const ColorRGBA kDim{168, 194, 184, 255};
const ColorRGBA kGold{246, 208, 88, 255};
const ColorRGBA kGround{15, 15, 23, 255};
const ColorRGBA kVeil{0, 0, 0, 150};
const ColorRGBA kTitle{23, 78, 66, 255};
const ColorRGBA kRule{43, 58, 54, 255};
const ColorRGBA kClay{206, 84, 31, 255};
const ColorRGBA kFrame{43, 99, 87, 255};
const ColorRGBA kSteel{60, 110, 98, 255};

/// What measures the text when there is nothing to draw on and no renderer was seen yet (every character 6 px wide: the IRenderer defaults)
class NoMetrics final : public IRenderer {
public:
    void draw_sprite(uint32_t, int32_t, int32_t, bool) override {}
    void draw_named_sprite(const std::string&, int32_t, int32_t, bool) override {}
    void fill_rect(int32_t, int32_t, int32_t, int32_t, assets::ColorRGBA) override {}
    void draw_rect(int32_t, int32_t, int32_t, int32_t, assets::ColorRGBA) override {}
    void draw_text(const std::string&, int32_t, int32_t, assets::ColorRGBA) override {}
    void set_hud_team(uint8_t) override {}
};

/// What measures and draws a line in the panel's units, and the walk's tools
struct Tools {
    IRenderer* draw;                  // null: only measuring
    const IRenderer* metrics;         // what measures
    const hud_art::Art* art;
    const GameMenuScale& s;
    bool plain;
    UnitMeter meter;
    UnitPen* pen;                     // null with nothing to draw on

    int32_t width(const std::string& text, double em) const { return meter.width(text, em); }

    void fill(int32_t ux, int32_t uy, int32_t uw, int32_t uh, ColorRGBA c) const {
        if (draw == nullptr) return;
        const LayoutRect r = s.rect(ux, uy, uw, uh);
        draw->fill_rect(r.x, r.y, r.w, r.h, c);
    }
    void outline(int32_t ux, int32_t uy, int32_t uw, int32_t uh, ColorRGBA c) const {
        if (draw == nullptr) return;
        const LayoutRect r = s.rect(ux, uy, uw, uh);
        draw->draw_rect(r.x, r.y, r.w, r.h, c);
    }
    /// A line in the game's face; align 0 left, 1 middle, 2 right of `ux`; `base` in units
    void text(const std::string& t, int32_t ux, int32_t base, ColorRGBA c, double em, int align = 0) const {
        if (pen != nullptr) pen->draw(t, s.px(ux), s.py(base), c, em, align);
    }
    /// A line in the windows' face (the titles and the words on the steel), with a shadow when asked
    void face(const std::string& t, int32_t ux, int32_t base, ColorRGBA c, double em, int align, bool shadow) const {
        if (draw == nullptr || t.empty()) return;
        if (!draw->dialog_text_ready()) {
            if (shadow) text(t, ux + 1, base + 1, ColorRGBA{10, 14, 12, 230}, em, align);
            text(t, ux, base, c, em, align);
            return;
        }
        const int32_t em_tenths = s.f > 0 ? static_cast<int32_t>(std::lround(em * s.f * 10.0)) : 100;
        const int32_t w = draw->dialog_text_width(t, em_tenths);
        const int32_t left = align == 0 ? s.px(ux) : align == 1 ? s.px(ux) - w / 2 : s.px(ux) - w;
        if (shadow) draw->draw_dialog_text(t, left + 1, s.py(base) + 1, ColorRGBA{10, 14, 12, 230}, em_tenths);
        draw->draw_dialog_text(t, left, s.py(base), c, em_tenths);
    }
    int32_t face_width(const std::string& t, double em) const {
        const int32_t em_tenths = static_cast<int32_t>(std::lround(em * s.f * 10.0));
        if (metrics->dialog_text_ready()) return metrics->dialog_text_width(t, em_tenths);
        return width(t, em);
    }

    const hud_art::Sprite* sprite(const char* name, int32_t& id) const {
        id = art != nullptr ? art->find_sprite(name) : -1;
        return art != nullptr ? art->sprite_at(id) : nullptr;
    }
    void put(const char* name, int32_t ux, int32_t uy) const {
        if (draw == nullptr || plain) return;
        int32_t id = -1;
        const hud_art::Sprite* sp = sprite(name, id);
        if (sp == nullptr) return;
        hud_art::draw_all(*draw, *art, {hud_art::Draw{id, s.rect(ux, uy, sp->w, sp->h), LayoutRect{0, 0, sp->w, sp->h}}});
    }
    void run_x(const std::string& name, int32_t x0, int32_t x1, int32_t y) const {
        if (draw == nullptr || plain) return;
        int32_t id = -1;
        const hud_art::Sprite* sp = sprite(name.c_str(), id);
        if (sp == nullptr) return;
        std::vector<hud_art::Draw> draws;
        for (int32_t x = x0; x < x1; x += sp->w) {
            const int32_t w = std::min(sp->w, x1 - x);
            draws.push_back(hud_art::Draw{id, s.rect(x, y, w, sp->h), LayoutRect{0, 0, w, sp->h}});
        }
        hud_art::draw_all(*draw, *art, draws);
    }
    void run_y(const char* name, int32_t x, int32_t y0, int32_t y1) const {
        if (draw == nullptr || plain) return;
        int32_t id = -1;
        const hud_art::Sprite* sp = sprite(name, id);
        if (sp == nullptr) return;
        std::vector<hud_art::Draw> draws;
        for (int32_t y = y0; y < y1; y += sp->h) {
            const int32_t h = std::min(sp->h, y1 - y);
            draws.push_back(hud_art::Draw{id, s.rect(x, y, sp->w, h), LayoutRect{0, 0, sp->w, h}});
        }
        hud_art::draw_all(*draw, *art, draws);
    }
    /// The clay panel and its frame
    void panel() const {
        if (draw == nullptr) return;
        if (plain) {
            fill(0, 0, kMenuPanelW, kMenuPanelH, kClay);
            outline(0, 0, kMenuPanelW, kMenuPanelH, kFrame);
            outline(3, 3, kMenuPanelW - 6, kMenuPanelH - 6, kFrame);
            return;
        }
        std::vector<hud_art::Draw> draws = hud_art::panel_draws(*art, LayoutRect{0, 0, kMenuPanelW, kMenuPanelH});
        for (hud_art::Draw& d : draws) d.dst = s.rect(d.dst.x, d.dst.y, d.dst.w, d.dst.h);
        hud_art::draw_all(*draw, *art, draws);
    }
    /// A well round the dark ground `g` (the ground, then the eight pieces of its bezel: 4 more left of and above it, 6 right of and below it)
    void well(int32_t x, int32_t y, int32_t w, int32_t h) const {
        fill(x, y, w, h, kGround);
        if (plain) {
            outline(x - 1, y - 1, w + 2, h + 2, kFrame);
            return;
        }
        const int32_t x0 = x - 4, y0 = y - 4, x1 = x + w + 6, y1 = y + h + 6, c = 12;
        run_x("sc_well_t", x0 + c, x1 - c, y0);
        run_x("sc_well_b", x0 + c, x1 - c, y1 - c);
        run_y("sc_well_l", x0, y0 + c, y1 - c);
        run_y("sc_well_r", x1 - c, y0 + c, y1 - c);
        put("sc_well_tl", x0, y0);
        put("sc_well_tr", x1 - c, y0);
        put("sc_well_bl", x0, y1 - c);
        put("sc_well_br", x1 - c, y1 - c);
    }
    /// A steel button of the small size (26 high): its ends and a run of the brushing, the word on it; `state` 0 up, 1 hover, 2 sunk (the chosen one: gold word)
    void strip(int32_t x, int32_t y, int32_t w, const std::string& word, int state, double em = 14.0) const {
        if (draw == nullptr) return;
        if (plain) {
            fill(x, y, w, kButtonH, state == 2 ? ColorRGBA{23, 56, 47, 255} : state == 1 ? ColorRGBA{58, 130, 112, 255} : kSteel);
            outline(x, y, w, kButtonH, ColorRGBA{8, 20, 16, 255});
        } else {
            static const char* const kState[3] = {"up", "hover", "down"};
            const std::string st = kState[std::clamp(state, 0, 2)];
            constexpr int32_t e = 12;
            run_x("sc_sm_m_" + st, x + e, x + w - e, y);
            put(("sc_sm_l_" + st).c_str(), x, y);
            put(("sc_sm_r_" + st).c_str(), x + w - e, y);
        }
        face(word, x + w / 2, y + 17 + (state == 2 ? 1 : 0), state == 2 ? kGold : kCream, em, 1, true);
    }
    /// The two switches of a row: On chosen or Off chosen
    void onoff(int32_t x, int32_t y, bool on, int hover, int pressed) const {   // hover / pressed: 0 none, 1 the On word, 2 the Off word
        if (draw == nullptr) return;
        if (plain) {
            strip(x, y, 48, "On", on ? 2 : (hover == 1 ? 1 : 0), 13.0);
            strip(x + 52, y, 48, "Off", !on ? 2 : (hover == 2 ? 1 : 0), 13.0);
            return;
        }
        const auto pick = [&](bool chosen, bool over, bool down, const char* base) {
            std::string name = std::string(base) + (chosen ? (over ? "_chosen_hover" : "_chosen") : down ? "_down" : over ? "_hover" : "_up");
            return name;
        };
        put(pick(on, hover == 1, pressed == 1, "op_on").c_str(), x, y);
        put(pick(!on, hover == 2, pressed == 2, "op_off").c_str(), x + 51, y);
    }
};

/// The words of a hint wrapped to `room` units: the lines (at most three)
std::vector<std::string> wrap_hint(const Tools& t, const std::string& hint, int32_t room, double em) {
    std::vector<std::string> lines;
    std::string line;
    size_t pos = 0;
    while (pos < hint.size()) {
        size_t end = hint.find(' ', pos);
        if (end == std::string::npos) end = hint.size();
        const std::string word = hint.substr(pos, end - pos);
        const std::string next = line.empty() ? word : line + " " + word;
        if (!line.empty() && t.width(next, em) > t.s.f * room) {
            lines.push_back(line);
            line = word;
        } else {
            line = next;
        }
        pos = end + 1;
    }
    if (!line.empty()) lines.push_back(line);
    if (lines.size() > 3) {
        lines.resize(3);
        lines.back() += "...";
    }
    return lines;
}

}  // namespace

// ---- the scale ------------------------------------------------------------------------------------------------------------------------------------------------

int32_t GameMenuScale::px(int32_t unit) const noexcept { return x + static_cast<int32_t>(std::lround(static_cast<double>(unit) * f)); }
int32_t GameMenuScale::py(int32_t unit) const noexcept { return y + static_cast<int32_t>(std::lround(static_cast<double>(unit) * f)); }
LayoutRect GameMenuScale::rect(int32_t ux, int32_t uy, int32_t uw, int32_t uh) const noexcept {
    const int32_t x0 = px(ux), y0 = py(uy), x1 = px(ux + uw), y1 = py(uy + uh);
    return LayoutRect{x0, y0, x1 - x0, y1 - y0};
}

GameMenuScale game_menu_scale(int32_t picture_w, int32_t picture_h, double output) noexcept {
    GameMenuScale s;
    const double o = output > 0.0 ? output : 1.0;
    const int32_t k = static_cast<int32_t>(std::floor(std::min(static_cast<double>(picture_w) * o / kMenuPanelW, static_cast<double>(picture_h) * o / kMenuPanelH) + 1e-6));
    if (k >= 1) {
        s.f = static_cast<double>(k) / o;
        s.sheet = std::clamp(k, 1, 3);
    } else {
        s.f = std::max(0.2, std::min(static_cast<double>(picture_w) / kMenuPanelW, static_cast<double>(picture_h) / kMenuPanelH));
        s.sheet = 1;
    }
    s.x = std::max(0, (picture_w - static_cast<int32_t>(std::lround(kMenuPanelW * s.f))) / 2);
    s.y = std::max(0, (picture_h - static_cast<int32_t>(std::lround(kMenuPanelH * s.f))) / 2);
    return s;
}

// ---- opening, closing and the sections ------------------------------------------------------------------------------------------------------------------------

void GameMenuPanel::build_sections() {
    sections_ = game_menu::sections_for(watching_);
}

void GameMenuPanel::open(bool watching) {
    watching_ = watching;
    build_sections();
    open_ = true;
    tab_ = 0;
    scroll_ = 0;
    focus_ = -1;
    asking_ = false;
    pressing_ = false;
    dragging_ = false;
    outside_press_ = false;
}

void GameMenuPanel::close() {
    if (!open_) return;
    open_ = false;
    pressing_ = false;
    dragging_ = false;
    focus_ = -1;
    asking_ = false;
    if (on_close_) on_close_();
}

void GameMenuPanel::set_picture(int32_t w, int32_t h, double output) {
    picture_w_ = std::max(1, w);
    picture_h_ = std::max(1, h);
    output_ = output > 0.0 ? output : 1.0;
    scale_ = game_menu_scale(picture_w_, picture_h_, output_);
}

void GameMenuPanel::select_tab(int index) {
    if (index < 0 || static_cast<size_t>(index) >= sections_.size()) return;
    tab_ = index;
    scroll_ = 0;
    focus_ = -1;
}

void GameMenuPanel::unfocus() { focus_ = -1; }

void GameMenuPanel::clamp_scroll() {
    const int32_t visible = kWellH - 8;
    scroll_ = std::clamp(scroll_, 0, std::max(0, content_h_ - visible));
}

// ---- the walk ---------------------------------------------------------------------------------------------------------------------------------------------------

void GameMenuPanel::paint(IRenderer* renderer, const hud_art::Art* art, std::vector<Item>* items) {
    scale_ = game_menu_scale(picture_w_, picture_h_, output_);
    const GameMenuScale& s = scale_;
    plain_ = art == nullptr || !window_art::has_menu_panel(*art);
    static const NoMetrics fallback;
    const IRenderer* metrics = renderer != nullptr ? renderer : (metrics_ != nullptr ? metrics_ : &fallback);
    UnitMeter meter(*metrics, s.f);
    std::unique_ptr<UnitPen> pen;
    if (renderer != nullptr) pen = std::make_unique<UnitPen>(*renderer, s.f);
    Tools t{renderer, metrics, art, s, plain_, meter, pen.get()};
    const auto add = [&](Part part, const LayoutRect& r, int row, int word) {
        if (items != nullptr) items->push_back(Item{part, r, row, word});
    };
    const auto over = [&](const LayoutRect& r) { return r.contains(pointer_x_, pointer_y_); };
    const auto is_pressed = [&](Part part, int row, int word) { return pressing_ && pressed_.part == part && pressed_.row == row && pressed_.word == word; };

    if (renderer != nullptr) {
        renderer->fill_rect(0, 0, picture_w_, picture_h_, kVeil);
        renderer->set_art_scale(plain_ ? 0 : s.sheet);
        t.panel();
        t.face("Settings", kInX, kInY + 30, kTitle, 30.0, 0, true);
        t.text("Esc closes", kInR, kInY + 26, ColorRGBA{31, 23, 51, 255}, 11.0, 2);
    }
    // the sections, one steel button each
    for (size_t i = 0; i < sections_.size(); ++i) {
        const int32_t uy = kTabTop + static_cast<int32_t>(i) * kTabPitch;
        const LayoutRect r = s.rect(kInX, uy, kTabW, kTabH);
        add(Part::Tab, r, static_cast<int>(i), 0);
        const int state = static_cast<int>(i) == tab_ ? 2 : (over(r) && !asking_ ? 1 : 0);
        t.strip(kInX, uy, kTabW, sections_[i].label, state);
    }
    // the section: its rows (or the list of the controls) in a well, scrolled
    t.well(kWellX, kWellY, kWellW, kWellH);
    const LayoutRect pane = s.rect(kWellX, kWellY + 4, kWellW, kWellH - 8);
    if (renderer != nullptr) renderer->set_clip_rect(pane.x, pane.y, pane.w, pane.h);
    int32_t y = kWellY + 6 - scroll_;
    const int32_t left = kWellX + kRowPad, right = kWellX + kWellW - kRowPad;
    const auto visible = [&](const LayoutRect& r) { return r.y < pane.bottom() && r.bottom() > pane.y; };
    const game_menu::Section section = sections_.empty() ? game_menu::Section::Sound : sections_[static_cast<size_t>(tab_)].section;
    int32_t content = 0;
    if (section == game_menu::Section::Controls) {
        const auto group = [&](const char* title, game_menu::ControlGroup g, bool keys) {
            t.text(title, left, y + 14, kGold, 11.0);
            y += 20;
            size_t n = 0;
            const game_menu::ControlLine* lines = game_menu::control_lines(g, n);
            for (size_t i = 0; i < n; ++i) {
                if (watching_ && lines[i].play_only) continue;
                if (!keys) {
                    t.text(lines[i].keys, left, y + 14, kCream, 13.0);
                } else {
                    int32_t x = left;
                    std::string part;
                    const std::string all = lines[i].keys;
                    size_t pos = 0;
                    while (pos <= all.size()) {
                        size_t bar = all.find('|', pos);
                        if (bar == std::string::npos) bar = all.size();
                        part = all.substr(pos, bar - pos);
                        if (!part.empty() && part[0] == '~') {
                            t.text(part.substr(1), x + 3, y + 14, kDim, 12.0);
                            x += static_cast<int32_t>(t.width(part.substr(1), 12.0) / s.f) + 8;
                        } else {
                            const int32_t w = static_cast<int32_t>(t.width(part, 12.0) / s.f) + 12;
                            t.fill(x, y + 2, w, 16, ColorRGBA{22, 34, 44, 255});
                            t.outline(x, y + 2, w, 16, ColorRGBA{60, 82, 98, 255});
                            t.text(part, x + w / 2, y + 14, kCream, 12.0, 1);
                            x += w + 4;
                        }
                        pos = bar + 1;
                    }
                }
                t.text(lines[i].what, right, y + 14, kDim, 12.0, 2);
                y += 22;
            }
            y += 6;
        };
        group("MOUSE", watching_ ? game_menu::ControlGroup::MouseWatch : game_menu::ControlGroup::Mouse, false);
        group("KEYS", game_menu::ControlGroup::Keys, true);
        t.text(std::string(watching_ ? "View only. " : "") + game_menu::controls_note(), left, y + 12, kDim, 11.0);
        y += 22;
        content = y - (kWellY + 6 - scroll_);
    } else {
        bool first = true;
        for (const game_menu::Row* row : game_menu::rows_for(section, watching_, false)) {
            // what the row needs at its right, to know how much room the words of its hint have
            std::vector<int32_t> word_w;
            int32_t control_w = 0;
            std::vector<std::string> words;
            if (row->kind == game_menu::Kind::Switch) {
                control_w = kSwitchW;
            } else if (row->kind == game_menu::Kind::Slider) {
                control_w = kSliderW + 70;
            } else if (row->kind == game_menu::Kind::Choice) {
                std::string all = row->words;
                size_t pos = 0;
                while (pos <= all.size()) {
                    size_t bar = all.find('|', pos);
                    if (bar == std::string::npos) bar = all.size();
                    words.push_back(all.substr(pos, bar - pos));
                    pos = bar + 1;
                }
                for (const std::string& w : words) {
                    word_w.push_back(std::max<int32_t>(56, static_cast<int32_t>(t.face_width(w, 13.0) / s.f) + 26));
                    control_w += word_w.back() + 6;
                }
            } else if (row->kind == game_menu::Kind::Button) {
                control_w = 140;
            } else if (row->kind == game_menu::Kind::Info && std::string(row->key) == "version") {
                control_w = 90;
            }
            const int32_t room = (row->kind == game_menu::Kind::Text ? 70 : right - left - control_w - 12);
            const std::vector<std::string> hint = row->hint[0] != '\0' ? wrap_hint(t, row->hint, row->kind == game_menu::Kind::Text ? right - left : room, 10.0) : std::vector<std::string>{};
            const int32_t height = std::max<int32_t>(kRowH, 26 + static_cast<int32_t>(hint.size()) * kHintLine + 4);
            const int32_t top = y;
            const LayoutRect row_rect = s.rect(kWellX, top, kWellW, height);
            const bool shown = visible(row_rect);
            if (!first && renderer != nullptr && shown) t.fill(kWellX + 10, top, kWellW - 20, 1, kRule);
            first = false;
            const bool text_row = row->kind == game_menu::Kind::Text;
            if (renderer != nullptr && shown) {
                t.text(row->label, left, top + 16, text_row ? kGold : kCream, text_row ? 15.0 : 14.0);
                if ((row->show & game_menu::kShowNew) != 0 && !text_row) {
                    const int32_t nx = left + static_cast<int32_t>(t.width(row->label, 14.0) / s.f) + 6;
                    t.fill(nx, top + 6, 28, 11, kGold);
                    t.text("NEW", nx + 14, top + 15, ColorRGBA{31, 23, 51, 255}, 8.0, 1);
                }
                for (size_t i = 0; i < hint.size() && !text_row; ++i) t.text(hint[i], left, top + 31 + static_cast<int32_t>(i) * kHintLine, kDim, 10.0);
            }
            const int32_t cy = top + 10;                           // the controls' top
            const int id = row->id;
            switch (row->kind) {
                case game_menu::Kind::Slider: {
                    const int32_t tx = right - 70 - kSliderW;
                    const LayoutRect track = s.rect(tx, cy, kSliderW, 18);
                    const int32_t value = std::clamp(host_.get(id), row->min, row->max);
                    const int32_t shown_value = dragging_ && pressed_.row == id ? drag_value_ : value;
                    if (shown) add(Part::Slider, track, id, 0);
                    if (renderer != nullptr && shown) {
                        t.fill(tx, cy + 6, kSliderW, 6, ColorRGBA{24, 60, 52, 255});
                        t.fill(tx, cy + 6, static_cast<int32_t>(kSliderW * static_cast<double>(shown_value) / std::max(1, row->max)), 6, ColorRGBA{52, 140, 112, 255});
                        t.outline(tx, cy + 6, kSliderW, 6, ColorRGBA{8, 24, 20, 255});
                        const int32_t thumb_x = tx + static_cast<int32_t>((kSliderW - 49) * static_cast<double>(shown_value) / std::max(1, row->max));
                        const bool held = dragging_ && pressed_.row == id;
                        t.put(plain_ ? "" : (held ? "op_thumb_held" : over(s.rect(thumb_x, cy, 49, 18)) ? "op_thumb_hover" : "op_thumb_up"), thumb_x, cy);
                        if (plain_) {
                            t.fill(thumb_x, cy, 49, 18, kSteel);
                            t.outline(thumb_x, cy, 49, 18, ColorRGBA{8, 20, 16, 255});
                        }
                        t.text(std::to_string(shown_value), right, cy + 14, kGold, 14.0, 2);
                    }
                    break;
                }
                case game_menu::Kind::Switch: {
                    const int32_t sx = right - kSwitchW;
                    const bool on = host_.get(id) == 1;
                    const LayoutRect on_rect = s.rect(sx, cy, 49, 24), off_rect = s.rect(sx + 51, cy, 49, 24);
                    if (shown) {
                        add(Part::Word, on_rect, id, 0);
                        add(Part::Word, off_rect, id, 1);
                    }
                    if (renderer != nullptr && shown) {
                        const int hover = asking_ ? 0 : over(on_rect) ? 1 : over(off_rect) ? 2 : 0;
                        const int down = is_pressed(Part::Word, id, 0) ? 1 : is_pressed(Part::Word, id, 1) ? 2 : 0;
                        t.onoff(sx, cy, on, hover, down);
                    }
                    break;
                }
                case game_menu::Kind::Choice: {
                    const int now = host_.get(id);
                    int32_t x = right;
                    for (size_t i = words.size(); i-- > 0;) {
                        x -= word_w[i];
                        const LayoutRect r = s.rect(x, cy, word_w[i], kButtonH);
                        if (shown) add(Part::Word, r, id, static_cast<int>(i));
                        if (renderer != nullptr && shown) t.strip(x, cy, word_w[i], words[i], static_cast<int>(i) == now ? 2 : (over(r) && !asking_ ? 1 : 0), 13.0);
                        x -= 6;
                    }
                    break;
                }
                case game_menu::Kind::Button: {
                    std::string word = row->words;
                    const size_t bar = word.find('|');
                    if (id == game_menu::kFullscreen && bar != std::string::npos) word = host_.fullscreen() ? word.substr(bar + 1) : word.substr(0, bar);
                    const int32_t w = std::max<int32_t>(110, static_cast<int32_t>(t.face_width(word, 13.0) / s.f) + 28);
                    const LayoutRect r = s.rect(right - w, cy, w, kButtonH);
                    if (shown) add(Part::Button, r, id, 0);
                    if (renderer != nullptr && shown) t.strip(right - w, cy, w, word, is_pressed(Part::Button, id, 0) ? 2 : (over(r) && !asking_ ? 1 : 0), 13.0);
                    break;
                }
                case game_menu::Kind::Info:
                    if (renderer != nullptr && shown && std::string(row->key) == "version") t.text(host_.version(), right, top + 16, kDim, 14.0, 2);
                    break;
                case game_menu::Kind::Text: {
                    const int32_t ex = left + 48, ew = right - ex;
                    const LayoutRect r = s.rect(ex, top + 11, ew, kEditH);
                    if (shown) add(Part::Edit, r, id, 0);
                    if (renderer != nullptr && shown) {
                        const bool focus = focus_ == id;
                        t.fill(ex, top + 11, ew, kEditH, ColorRGBA{8, 8, 14, 255});
                        t.outline(ex, top + 11, ew, kEditH, focus ? ColorRGBA{243, 219, 55, 255} : ColorRGBA{92, 110, 104, 255});
                        std::string text = host_.text(id);
                        // a text that does not fit shows its end, as the original's field does
                        while (!text.empty() && t.width(text, 13.0) > s.f * (ew - 14)) text.erase(0, 1);
                        t.text(text, ex + 6, top + 11 + 15, kCream, 13.0);
                        if (focus && ((now_ms_ / 500u) % 2u) == 0u) {
                            const int32_t cx = ex + 7 + static_cast<int32_t>(t.width(text, 13.0) / s.f);
                            t.fill(cx, top + 11 + 4, 1, kEditH - 8, kCream);
                        }
                    }
                    break;
                }
            }
            y += height;
        }
        content = y - (kWellY + 6 - scroll_) + 6;
    }
    if (renderer != nullptr) renderer->clear_clip_rect();
    content_h_ = content;
    // the foot: the note and the two buttons
    const bool question = asking_;
    const LayoutRect resume = s.rect(kInR - 118 - 8 - 128, kFootY, 118, kButtonH), leave = s.rect(kInR - 128, kFootY, 128, kButtonH);
    add(Part::Resume, resume, 0, 0);
    add(Part::Leave, leave, 0, 0);
    if (renderer != nullptr) {
        t.text(watching_ ? "The replay keeps playing." : "The game keeps running while this menu is open.", kInX, kInB - 8, ColorRGBA{31, 23, 51, 255}, 12.0);
        t.strip(kInR - 118 - 8 - 128, kFootY, 118, "Resume", is_pressed(Part::Resume, 0, 0) ? 2 : (over(resume) && !question ? 1 : 0), 15.0);
        t.strip(kInR - 128, kFootY, 128, "Leave game", is_pressed(Part::Leave, 0, 0) ? 2 : (over(leave) && !question ? 1 : 0), 15.0);
    }
    // the question of Leave game, in a card of its own
    if (question) {
        const int32_t cw = 420, ch = 176, cx = (kMenuPanelW - cw) / 2, cyy = (kMenuPanelH - ch) / 2;
        const game_menu::LeaveWords words = game_menu::leave_words(static_cast<game_menu::LeaveKind>(ask_kind_));
        if (renderer != nullptr) {
            t.fill(0, 0, kMenuPanelW, kMenuPanelH, ColorRGBA{0, 0, 0, 140});
            t.fill(cx, cyy, cw, ch, ColorRGBA{5, 9, 12, 255});
            t.outline(cx, cyy, cw, ch, ColorRGBA{179, 38, 46, 255});
            t.outline(cx + 2, cyy + 2, cw - 4, ch - 4, ColorRGBA{179, 38, 46, 255});
            t.face(words.title, cx + 20, cyy + 36, ColorRGBA{255, 141, 127, 255}, 24.0, 0, false);
            const std::vector<std::string> body = wrap_hint(t, words.body, cw - 40, 14.0);
            for (size_t i = 0; i < body.size(); ++i) t.text(body[i], cx + 20, cyy + 62 + static_cast<int32_t>(i) * 18, kCream, 14.0);
            t.text("The game keeps running while you decide.", cx + 20, cyy + ch - 52, kDim, 11.0);
        }
        const LayoutRect stay = s.rect(cx + 20, cyy + ch - 40, 170, kButtonH), go = s.rect(cx + cw - 20 - 140, cyy + ch - 40, 140, kButtonH);
        if (items != nullptr) items->clear();
        add(Part::Stay, stay, 0, 0);
        add(Part::Go, go, 0, 0);
        if (renderer != nullptr) {
            t.strip(cx + 20, cyy + ch - 40, 170, "Stay in the game", is_pressed(Part::Stay, 0, 0) ? 2 : over(stay) ? 1 : 0, 14.0);
            t.strip(cx + cw - 20 - 140, cyy + ch - 40, 140, words.go, is_pressed(Part::Go, 0, 0) ? 2 : over(go) ? 1 : 0, 14.0);
        }
    }
    if (renderer != nullptr) renderer->set_art_scale(0);
}

void GameMenuPanel::render(IRenderer& renderer, const hud_art::Art* art, uint32_t now_ms) {
    if (!open_) return;
    now_ms_ = now_ms;
    metrics_ = &renderer;
    set_picture(picture_w_, picture_h_, renderer.output_scale());
    paint(&renderer, art, nullptr);
    clamp_scroll();
}

std::vector<GameMenuPanel::Item> GameMenuPanel::items() {
    std::vector<Item> out;
    if (!open_) return out;
    paint(nullptr, nullptr, &out);
    return out;
}

// ---- the input ----------------------------------------------------------------------------------------------------------------------------------------------------

int GameMenuPanel::item_at(const std::vector<Item>& list, int32_t x, int32_t y) const {
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].rect.contains(x, y)) return static_cast<int>(i);
    }
    return -1;
}

void GameMenuPanel::slide_to(const Item& item, int32_t x, bool save) {
    const game_menu::Row* row = game_menu::find_row(item.row);
    if (row == nullptr) return;
    const double span = static_cast<double>(std::max(1, item.rect.w - static_cast<int32_t>(std::lround(49.0 * scale_.f))));
    const double at = std::clamp((static_cast<double>(x) - static_cast<double>(item.rect.x) - 24.0 * scale_.f) / span, 0.0, 1.0);
    const int32_t value = static_cast<int32_t>(std::lround(at * static_cast<double>(row->max - row->min))) + row->min;
    drag_value_ = value;
    if ((row->show & game_menu::kShowLive) != 0 || save) host_.set(row->id, value, save);
}

void GameMenuPanel::ask_leave() {
    ask_kind_ = host_.get(game_menu::kFactLeave);
    if (ask_kind_ < 0) ask_kind_ = static_cast<int>(game_menu::LeaveKind::EndsMatch);
    if (!game_menu::leave_needs_confirm(static_cast<game_menu::LeaveKind>(ask_kind_))) {
        close();
        host_.act(game_menu::kActLeave);
        return;
    }
    asking_ = true;
    focus_ = -1;
}

void GameMenuPanel::fire(const Item& item) {
    switch (item.part) {
        case Part::Tab:
            select_tab(item.row);
            break;
        case Part::Word: {
            const game_menu::Row* row = game_menu::find_row(item.row);
            if (row == nullptr) break;
            const int value = row->kind == game_menu::Kind::Switch ? (item.word == 0 ? 1 : 0) : item.word;
            host_.set(row->id, value, true);
            break;
        }
        case Part::Button:
            if (item.row == game_menu::kQuickHelp) {
                close();
                host_.act(game_menu::kActQuickHelp);
            } else if (item.row == game_menu::kFullscreen) {
                host_.act(game_menu::kActFullscreen);
            }
            break;
        case Part::Resume:
            close();
            break;
        case Part::Leave:
            ask_leave();
            break;
        case Part::Stay:
            asking_ = false;
            break;
        case Part::Go:
            close();
            host_.act(game_menu::kActLeave);
            break;
        case Part::Slider:
        case Part::Edit:
            break;
    }
}

bool GameMenuPanel::on_mouse_move(int32_t x, int32_t y) {
    if (!open_) return false;
    pointer_x_ = x;
    pointer_y_ = y;
    if (dragging_ && pressing_) slide_to(pressed_, x, false);
    return true;
}

bool GameMenuPanel::on_mouse_down(int32_t x, int32_t y, uint8_t button) {
    if (!open_) return false;
    pointer_x_ = x;
    pointer_y_ = y;
    if (button != 1) return true;                                       // (SDL_BUTTON_LEFT: only the left button acts)
    const std::vector<Item> list = items();
    const int at = item_at(list, x, y);
    outside_press_ = !asking_ && !panel_rect().contains(x, y);
    pressing_ = false;
    dragging_ = false;
    if (at < 0) {
        if (!asking_ && !panel_rect().contains(x, y)) return true;
        unfocus();
        return true;
    }
    const Item& item = list[static_cast<size_t>(at)];
    pressed_ = item;
    pressing_ = true;
    if (item.part == Part::Edit) {
        focus_ = item.row;
        pressing_ = false;
    } else {
        if (focus_ >= 0) unfocus();
        if (item.part == Part::Slider) {
            dragging_ = true;
            slide_to(item, x, false);
        }
    }
    return true;
}

bool GameMenuPanel::on_mouse_up(int32_t x, int32_t y, uint8_t button) {
    if (!open_) return false;
    pointer_x_ = x;
    pointer_y_ = y;
    if (button != 1) return true;
    if (outside_press_) {
        outside_press_ = false;
        if (!panel_rect().contains(x, y) && !asking_) close();           // a press and a release on the dim close the panel, as the page's do
        return true;
    }
    if (!pressing_) return true;
    const Item item = pressed_;
    pressing_ = false;
    if (item.part == Part::Slider) {
        dragging_ = false;
        slide_to(item, x, true);
        return true;
    }
    const std::vector<Item> list = items();
    const int at = item_at(list, x, y);
    if (at >= 0 && list[static_cast<size_t>(at)].part == item.part && list[static_cast<size_t>(at)].row == item.row && list[static_cast<size_t>(at)].word == item.word) fire(item);
    return true;
}

void GameMenuPanel::cancel_press() {
    pressing_ = false;
    outside_press_ = false;
    if (dragging_) {
        dragging_ = false;
        const game_menu::Row* row = game_menu::find_row(pressed_.row);
        if (row != nullptr) host_.set(row->id, drag_value_, true);       // (the value where the drag stood is kept: the thumb is where the sound is)
    }
}

bool GameMenuPanel::on_wheel(int32_t dy) {
    if (!open_ || asking_) return open_;
    scroll_ -= dy * 24;
    clamp_scroll();
    return true;
}

bool GameMenuPanel::on_key(int32_t key, bool repeat) {
    if (!open_) return false;
    constexpr int32_t kEsc = SDLK_ESCAPE, kEnter = SDLK_RETURN, kKpEnter = SDLK_KP_ENTER, kBackspace = SDLK_BACKSPACE, kTab = SDLK_TAB, kUp = SDLK_UP, kDown = SDLK_DOWN;
    if (key == kEsc) {
        if (repeat) return true;                                          // (a held Esc does not close it)
        if (asking_) asking_ = false;
        else close();
        return true;
    }
    if (asking_) {
        if ((key == kEnter || key == kKpEnter) && !repeat) asking_ = false;                // Stay is the default
        return true;
    }
    if (focus_ >= 0) {
        if (key == kBackspace) {
            std::string text = host_.text(focus_);
            if (!text.empty()) text.pop_back();
            host_.set_text(focus_, text);
        } else if ((key == kEnter || key == kKpEnter) && !repeat) {
            unfocus();
        } else if (key == kTab && !repeat) {
            const std::vector<Item> list = items();
            std::vector<int> ids;
            for (const Item& it : list) {
                if (it.part == Part::Edit) ids.push_back(it.row);
            }
            if (!ids.empty()) {
                const auto at = std::find(ids.begin(), ids.end(), focus_);
                focus_ = at == ids.end() || at + 1 == ids.end() ? ids.front() : *(at + 1);
            }
        }
        return true;
    }
    if ((key == kEnter || key == kKpEnter) && !repeat) {
        close();                                                          // Resume is the default
    } else if (key == kUp && !repeat) {
        select_tab(tab_ - 1);
    } else if (key == kDown && !repeat) {
        select_tab(tab_ + 1);
    }
    return true;
}

bool GameMenuPanel::on_text(const std::string& text) {
    if (!open_) return false;
    if (focus_ < 0 || asking_) return true;
    std::string now = host_.text(focus_);
    for (const char c : text) {
        if (c >= 0x20 && c <= 0x7e && static_cast<int>(now.size()) < game_menu::kQuickChatMax) now += c;
    }
    host_.set_text(focus_, now);
    return true;
}

}  // namespace ants::app
