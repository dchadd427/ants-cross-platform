// The results screen in the new look (scorecard.hpp, scorecard_layout.hpp): the clay panel over the whole canvas, the title and the headings in the HUD art's cast-metal letters, the rows in wells, the names
// and numbers in the game's face, and Leave Game as a machined button. The layout is in units (a unit is a whole number of device pixels); the rows are the same as the original's page has.
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "ants_app/renderer.hpp"
#include "ants_app/scorecard.hpp"
#include "ants_app/text_layout.hpp"
#include "ants_app/unit_pen.hpp"
#include "ants_app/window_art.hpp"
#include "ants_sim/game_strings.hpp"

namespace ants::app {

namespace {

using namespace scorecard_layout;

const assets::ColorRGBA kCream{244, 236, 226, 255};          // the names and numbers on the dark ground
const assets::ColorRGBA kGold{246, 208, 88, 255};            // the winner's score (the countdown card's gold)
const assets::ColorRGBA kDim{168, 194, 184, 255};            // the small labels under a card's numbers
const assets::ColorRGBA kGround{15, 15, 23, 255};            // a well's dark ground (an edit field's)
const assets::ColorRGBA kDivider{43, 58, 54, 255};           // the hairline between two of the others' rows
constexpr double kLabelEm = 8.6;
constexpr double kHeadlineEm = 13.0;
constexpr double kWaitingEm = 15.0;
constexpr int32_t kAntGap = 27;                              // the second ant of an alliance stands this far right of the first
constexpr int32_t kAntSingleShift = 13;                      // one ant stands in the middle of the two's room

/// The art's pieces, placed in units
class Page {
public:
    Page(IRenderer& renderer, const hud_art::Art& art, const Scale& scale) : r_(renderer), art_(art), s_(scale) {}

    const hud_art::Sprite* sprite(const std::string& name, int32_t& id) const {
        id = art_.find_sprite(name);
        return art_.sprite_at(id);
    }
    /// A sprite at (ux, uy), its own size
    void put(const std::string& name, int32_t ux, int32_t uy) const {
        int32_t id = -1;
        const hud_art::Sprite* sp = sprite(name, id);
        if (sp == nullptr) return;
        hud_art::draw_all(r_, art_, {hud_art::Draw{id, s_.rect(ux, uy, sp->w, sp->h), LayoutRect{0, 0, sp->w, sp->h}}});
    }
    /// A line of lettering: the sprite's alignment point (its manifest place) at (anchor_x, baseline)
    void lettering(const std::string& name, int32_t anchor_x, int32_t baseline) const {
        int32_t id = -1;
        const hud_art::Sprite* sp = sprite(name, id);
        if (sp != nullptr) put(name, anchor_x - sp->x, baseline - sp->y);
    }
    /// A playful title centered on cx (the middle of its sprite is the middle of the line)
    void centered(const std::string& name, int32_t cx, int32_t baseline) const {
        int32_t id = -1;
        const hud_art::Sprite* sp = sprite(name, id);
        if (sp != nullptr) put(name, cx - sp->w / 2, baseline - sp->y);
    }
    /// The width of a lettering sprite (0 without it)
    int32_t width_of(const std::string& name) const {
        int32_t id = -1;
        const hud_art::Sprite* sp = sprite(name, id);
        return sp != nullptr ? sp->w : 0;
    }
    /// A sprite repeated along x from x0 to x1 (its last copy cut), or along y from y0 to y1
    void run_x(const std::string& name, int32_t x0, int32_t x1, int32_t y) const {
        int32_t id = -1;
        const hud_art::Sprite* sp = sprite(name, id);
        if (sp == nullptr) return;
        std::vector<hud_art::Draw> draws;
        for (int32_t x = x0; x < x1; x += sp->w) {
            const int32_t w = std::min(sp->w, x1 - x);
            draws.push_back(hud_art::Draw{id, s_.rect(x, y, w, sp->h), LayoutRect{0, 0, w, sp->h}});
        }
        hud_art::draw_all(r_, art_, draws);
    }
    void run_y(const std::string& name, int32_t x, int32_t y0, int32_t y1) const {
        int32_t id = -1;
        const hud_art::Sprite* sp = sprite(name, id);
        if (sp == nullptr) return;
        std::vector<hud_art::Draw> draws;
        for (int32_t y = y0; y < y1; y += sp->h) {
            const int32_t h = std::min(sp->h, y1 - y);
            draws.push_back(hud_art::Draw{id, s_.rect(x, y, sp->w, h), LayoutRect{0, 0, sp->w, h}});
        }
        hud_art::draw_all(r_, art_, draws);
    }
    /// A well round the dark ground `g`: the ground, then the eight pieces of its bezel (the picture is the ground with 4 more left of and above it and 6 right of and below it)
    void well(const LayoutRect& g) const {
        if (g.w <= 0 || g.h <= 0) return;
        const LayoutRect gr = s_.rect(g);
        r_.fill_rect(gr.x, gr.y, gr.w, gr.h, kGround);
        const int32_t x0 = g.x - 4, y0 = g.y - 4, x1 = g.x + g.w + 6, y1 = g.y + g.h + 6;
        const int32_t c = kWellCorner;
        run_x("sc_well_t", x0 + c, x1 - c, y0);
        run_x("sc_well_b", x0 + c, x1 - c, y1 - c);
        run_y("sc_well_l", x0, y0 + c, y1 - c);
        run_y("sc_well_r", x1 - c, y0 + c, y1 - c);
        put("sc_well_tl", x0, y0);
        put("sc_well_tr", x1 - c, y0);
        put("sc_well_bl", x0, y1 - c);
        put("sc_well_br", x1 - c, y1 - c);
    }
    /// The big Leave Game button: its left end, a run of the brushing, its right end, and the word
    void big_button(const LayoutRect& face, const char* state) const {
        const std::string st = state;
        const int32_t x0 = face.x, x1 = face.x + face.w + 3, e = kBigEnd;
        run_x("sc_btn_m_" + st, x0 + e, x1 - e, face.y);
        put("sc_btn_l_" + st, x0, face.y);
        put("sc_btn_r_" + st, x1 - e, face.y);
        lettering("sc_leave_word_" + st, face.x + face.w / 2, face.y + 26);
    }

private:
    IRenderer& r_;
    const hud_art::Art& art_;
    const Scale& s_;
};

/// The wrapped lines of the 187 headline in the windows' face (the page's own text as a last resort): at most two, the end of the second cut with three dots
std::vector<std::string> headline_lines(IRenderer& renderer, const UnitPen& pen, const Scale& scale, const std::string& text, int32_t max_w_units) {
    const int32_t max_w = static_cast<int32_t>(std::lround(static_cast<double>(max_w_units) * scale.f));
    const bool face = renderer.dialog_text_ready();
    const auto width = [&](const std::string& s) { return face ? renderer.dialog_text_width(s, scale.em_tenths(kHeadlineEm)) : pen.width(s, kHeadlineEm); };
    std::vector<std::string> lines = wrap_label_with(width, text, max_w);
    if (lines.size() > 2) {
        std::string rest = lines[1];
        for (size_t i = 2; i < lines.size(); ++i) rest += " " + lines[i];
        lines.resize(2);
        while (!rest.empty() && width(rest + "...") > max_w) rest.pop_back();
        lines[1] = rest + "...";
    }
    return lines;
}

}  // namespace

// One AntSlot as the new page draws it: the animation agst301 in the color of the team, running since the screen was created, its first frame's box (the whole picture of the ant) put at (ux, uy) units
// and drawn `a` times its own size (1: a pixel of the ant is a unit; less where a row is lower than the ant)
static void draw_ant_new(IRenderer& renderer, const assets::AssetArchive& assets, const Scale& scale, uint8_t team, double elapsed_ms, double ux, double uy, double a) {
    const auto* anim = assets.find_animation("agst301");
    if (anim == nullptr || anim->subitems.empty()) return;
    int32_t bx = INT32_MAX, by = INT32_MAX;
    for (const auto& part : anim->subitems[0].frames) {
        bx = std::min(bx, static_cast<int32_t>(part.dx));
        by = std::min(by, static_cast<int32_t>(part.dy));
    }
    const size_t frame = Renderer::get_anim_subitem_by_time(*anim, static_cast<uint32_t>(elapsed_ms));
    const auto& parts = anim->subitems[frame].frames;
    renderer.set_hud_team(team);
    const auto edge = [&](double u) { return static_cast<int32_t>(std::lround(u * scale.f)); };
    for (size_t k = parts.size(); k-- > 0;) {
        const assets::Sprite& sp = assets.get_sprite(parts[k].sprite_index);
        const double left = ux + (parts[k].dx - bx) * a;
        const double top = uy + (parts[k].dy - by) * a;
        const int32_t x0 = edge(left), y0 = edge(top);
        const int32_t w = std::max(1, edge(left + sp.width * a) - x0), h = std::max(1, edge(top + sp.height * a) - y0);
        renderer.draw_sprite_region(parts[k].sprite_index, x0, y0, w, h, 0, 0, static_cast<int32_t>(sp.width), static_cast<int32_t>(sp.height));
    }
    renderer.set_hud_team(0);
}

// The height of the box of the first frame of the ant's animation (the picture of the ant, whatever its frames do)
static int32_t ant_height(const assets::AssetArchive& assets) {
    const auto* anim = assets.find_animation("agst301");
    if (anim == nullptr || anim->subitems.empty() || anim->subitems[0].frames.empty()) return 42;
    int32_t y0 = INT32_MAX, y1 = INT32_MIN;
    for (const auto& part : anim->subitems[0].frames) {
        const assets::Sprite& sp = assets.get_sprite(part.sprite_index);
        y0 = std::min(y0, static_cast<int32_t>(part.dy));
        y1 = std::max(y1, static_cast<int32_t>(part.dy) + static_cast<int32_t>(sp.height));
    }
    return y1 - y0;
}

void ScorecardModal::render_new(IRenderer& renderer, const assets::AssetArchive& assets) {
    // the output may have changed since the application last said (a window dragged to another screen): the page follows
    const double output = renderer.output_scale();
    if (std::fabs(output - output_) > 1e-9) set_screen(canvas_w_, canvas_h_, output);
    const bool rows_shown = phase_ == Phase::Rows;
    UnitPen pen(renderer, scale_.f);
    if (new_look_ && mode_ == sim::GameMode::Kills187 && rows_shown && !headline_.empty()) {
        const int32_t lines = static_cast<int32_t>(headline_lines(renderer, pen, scale_, headline_, layout_.headline_w).size());
        if (lines != headline_lines_) {
            headline_lines_ = lines;
            update_new_look();
        }
    }
    if (!new_look_) {                                             // (the output became too small for the page: the original's)
        render(renderer, assets);
        return;
    }
    const bool is_187 = mode_ == sim::GameMode::Kills187;
    const scorecard_layout::Layout& l = layout_;
    const hud_art::Art& art = *art_;
    renderer.set_art_scale(scale_.sheet);
    Page page(renderer, art, scale_);

    // 1. The panel, over the whole canvas, and the banner
    std::vector<hud_art::Draw> panel = hud_art::panel_draws(art, LayoutRect{0, 0, scale_.w, scale_.h});
    for (hud_art::Draw& d : panel) d.dst = scale_.rect(d.dst);
    hud_art::draw_all(renderer, art, panel);
    page.centered("sc_title", l.title_cx, l.title_base);
    if (l.ok) {
        const char* sizes[] = {"sc_score_s", "sc_score_28", "sc_score_33", "sc_score_38"};
        const std::string score = sizes[static_cast<size_t>(l.score)];
        if (l.score_centered) page.centered(score, l.score_x, l.score_base);
        else page.lettering(score, l.score_x, l.score_base);

        // the column headings of a table (the 187 page has its own three)
        if (l.headings) {
            static const char* const kHeads[4] = {"sc_h_score", "sc_h_friendly", "sc_h_enemy", "sc_h_new"};
            static const char* const kSubs[4] = {"", "sc_s_lost", "sc_s_killed", "sc_s_hatched"};
            static const char* const kHeads187[3] = {"sc_h_kills", "sc_h_antslost", "sc_h_antsleft"};
            for (size_t c = 0; c < (is_187 ? 3u : 4u); ++c) {
                page.lettering(is_187 ? kHeads187[c] : kHeads[c], l.column_right[c], l.heading_base);
                if (!is_187 && kSubs[c][0] != '\0') page.lettering(kSubs[c], l.column_right[c], l.sub_base);
            }
        }

        // 2. The labels and the wells
        page.lettering("sc_winner", l.label_x, l.winner_base);
        page.well(l.winner_well);
        if (l.others_well.w > 0) {
            page.lettering("sc_others", l.label_x, l.others_base);
            page.well(l.others_well);
        }
        if (is_187 && rows_shown && !headline_.empty()) {
            const std::vector<std::string> lines = headline_lines(renderer, pen, scale_, headline_, l.headline_w);
            for (size_t i = 0; i < lines.size(); ++i) {
                const int32_t baseline = scale_.y(l.headline_base + static_cast<int32_t>(i) * l.headline_pitch);
                if (renderer.dialog_text_ready()) {
                    const int32_t em = scale_.em_tenths(kHeadlineEm);
                    renderer.draw_dialog_text(lines[i], scale_.x(l.headline_cx) - renderer.dialog_text_width(lines[i], em) / 2, baseline, window_art::kInk, em);
                } else {
                    pen.draw(lines[i], scale_.x(l.headline_cx), baseline, window_art::kInk, kHeadlineEm, 1);
                }
            }
        }

        // 3. While the scores are awaited: the sentence in the well of the others (of the winner when there are no others), and nothing else
        if (!rows_shown) {
            const LayoutRect& well = l.others_well.w > 0 ? l.others_well : l.winner_well;
            pen.draw(sim::strings::text(sim::strings::kWaitingForScores), scale_.x(l.row[0].name_x), scale_.y(well.y + 28), kCream, kWaitingEm, 0);
        } else {
            // 4. The rows: the ants, the name and the numbers (a card: the little labels under them)
            const int32_t ant_h = ant_height(assets);
            for (size_t i = 0; i < rows_.size() && i < l.row_count; ++i) {
                const Row& row = rows_[i];
                const RowSlots& r = l.row[i];
                if (i >= 2) {
                    const LayoutRect hair = scale_.rect(l.divider_x, r.top, l.divider_w, 1);
                    renderer.fill_rect(hair.x, hair.y, hair.w, std::max(1, hair.h), kDivider);
                }
                const double a = ant_h + 2 <= r.height ? 1.0 : static_cast<double>(r.height - 2) / static_cast<double>(ant_h);
                const double ant_top = r.top + (r.height - ant_h * a) / 2.0;
                const double ant_left = r.ants_left + (row.has_second() ? 0 : kAntSingleShift);
                draw_ant_new(renderer, assets, scale_, row.first, elapsed_ms_, ant_left, ant_top, a);
                if (row.has_second()) draw_ant_new(renderer, assets, scale_, row.second, elapsed_ms_, ant_left + kAntGap, ant_top, a);
                const double em = l.unit_text;
                pen.draw(pen.fit(row.name, scale_.x(r.name_x + r.name_w) - scale_.x(r.name_x), em), scale_.x(r.name_x), scale_.y(r.name_base), kCream, em, 0);
                static const char* const kShort[4] = {"Score", "Lost", "Killed", "Hatched"};
                static const char* const kShort187[3] = {"Kills", "Lost", "Left"};
                for (size_t c = 0; c < (is_187 ? 3u : 4u); ++c) {
                    const std::string& n = row.numbers[c];
                    const int32_t room = scale_.x(r.number_w) - scale_.x(0);
                    const int32_t w = pen.width(n, em);
                    const double fit_em = w > room && w > 0 ? em * room / w : em;                  // (a number too long for its place is set smaller: nothing is cut)
                    const int32_t x = scale_.x(r.number_x[c]);
                    pen.draw(n, x, scale_.y(r.number_base), i == 0 && c == 0 ? kGold : kCream, fit_em, l.narrow ? 1 : 2);
                    if (l.narrow) pen.draw(is_187 ? kShort187[c] : kShort[c], x, scale_.y(r.label_base), kDim, kLabelEm, 1);
                }
            }

            // 5. Leave Game
            const char* state = quit_.pressed() ? "down" : (quit_.hovered() ? "hover" : "up");
            if (l.leave_big) page.big_button(l.leave, state);
            else page.put(std::string("sc_leave_") + state, l.leave.x, l.leave.y);
        }
    }
    renderer.set_art_scale(0);
}

}  // namespace ants::app
