// The results screen in the new look (scorecard.hpp, scorecard_layout.hpp): the podium (Victory! or Defeat, the winner's well, the ranked others, the honors and the Play of the game), the waiting page, the
// second page "Match stats", and the buttons between them. The layout is in units (a unit is a whole number of device pixels); the rows are the same as the original's page has, and the numbers that were counted
// (match_report.hpp) are shown only when the count is whole: without them the page shows the original's numbers and nothing else.
#include <algorithm>
#include <cmath>
#include <cstdio>
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
using Page = ScorecardModal::Page;
using Look = ScorecardModal::Look;

const assets::ColorRGBA kCream{244, 236, 226, 255};          // the names and numbers on the dark ground
const assets::ColorRGBA kGold{246, 208, 88, 255};            // the winner's score, the best of a column (the countdown card's gold)
const assets::ColorRGBA kDim{168, 194, 184, 255};            // the small labels
const assets::ColorRGBA kGround{15, 15, 23, 255};            // a well's dark ground (an edit field's)
const assets::ColorRGBA kPicture{40, 36, 46, 255};           // the ground of the Play of the game's picture
const assets::ColorRGBA kDivider{43, 58, 54, 255};           // the hairline between two rows
const assets::ColorRGBA kRowDivider{93, 122, 112, 255};      // the line between two rows of the second page (an alliance's two players have a fainter one)
const assets::ColorRGBA kTrack{27, 41, 37, 255};             // the track of a score bar
const assets::ColorRGBA kTeam[4] = {{79, 191, 122, 255}, {224, 86, 79, 255}, {106, 124, 240, 255}, {185, 188, 198, 255}};      // the four colors of the ants: green, red, blue, grey

/// The art's pieces, placed in units
class Sheet {
public:
    Sheet(IRenderer& renderer, const hud_art::Art& art, const Scale& scale) : r_(renderer), art_(art), s_(scale) {}

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
    /// A button as wide as `face`: its left end, a run of the brushing, its right end (the picture is the face with the cast shadow 3 across)
    void strip(const std::string& prefix, const LayoutRect& face, const char* state) const {
        const std::string st = state;
        const int32_t x0 = face.x, x1 = face.x + face.w + 3, e = kBigEnd;
        run_x(prefix + "_m_" + st, x0 + e, x1 - e, face.y);
        put(prefix + "_l_" + st, x0, face.y);
        put(prefix + "_r_" + st, x1 - e, face.y);
    }

private:
    IRenderer& r_;
    const hud_art::Art& art_;
    const Scale& s_;
};

const char* state_of(Look look) { return look == Look::Down ? "down" : look == Look::Hover ? "hover" : "up"; }

/// Everything that a page is drawn with
struct Ctx {
    IRenderer& r;
    const hud_art::Art& art;
    const Scale& s;
    const assets::AssetArchive& assets;
    const ScorecardModal& m;
    const Sheet sheet;
    const UnitPen pen;
    Ctx(IRenderer& renderer, const hud_art::Art& a, const Scale& scale, const assets::AssetArchive& archive, const ScorecardModal& modal)
        : r(renderer), art(a), s(scale), assets(archive), m(modal), sheet(renderer, a, scale), pen(renderer, scale.f) {}
    int32_t X(int32_t u) const { return s.x(u); }
    int32_t Y(int32_t u) const { return s.y(u); }
    int32_t W(int32_t u) const { return X(u) - X(0); }
};

/// 1,240: a count with its thousands in groups
std::string grouped(int64_t value) {
    std::string digits = std::to_string(value < 0 ? -value : value);
    std::string out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return value < 0 ? "-" + out : out;
}

/// 12:40 for 15,200 turns (20 a second)
std::string clock_of(uint32_t turns) {
    const uint32_t seconds = turns / MatchReport::kTurnsPerSecond;
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%u:%02u", static_cast<unsigned>(seconds / 60), static_cast<unsigned>(seconds % 60));
    return buf;
}

std::string ordinal(int place) {          // 1-based
    const int tens = place % 100;
    const char* suffix = (tens >= 11 && tens <= 13) ? "th" : place % 10 == 1 ? "st" : place % 10 == 2 ? "nd" : place % 10 == 3 ? "rd" : "th";
    return std::to_string(place) + suffix;
}

/// A text that was cut ends in three dots right after its last letter ("Third..." and not "Third ...")
std::string tidy_dots(std::string text) {
    if (text.size() < 3 || text.compare(text.size() - 3, 3, "...") != 0) return text;
    size_t keep = text.size() - 3;
    while (keep > 0 && text[keep - 1] == ' ') --keep;
    text.erase(keep, text.size() - 3 - keep);
    return text;
}

/// The text cut at the end with three dots, whole characters, until it is no wider than `max_w` canvas pixels, in the windows' face (the game's own text where the renderer has no such face)
std::string fit_dialog(const IRenderer& r, const UnitMeter& meter, std::string text, int32_t max_w, double em_units, int32_t em_tenths) {
    const bool face = r.dialog_text_ready();
    const auto width = [&](const std::string& t) { return face ? r.dialog_text_width(t, em_tenths) : meter.width(t, em_units); };
    if (width(text) <= max_w) return text;
    while (!text.empty() && width(text + "...") > max_w) UnitMeter::cut_last_character(text);
    return tidy_dots(text + "...");
}

/// A text of the face cut with three dots to the room (canvas pixels), at whole characters
std::string fit_pen(const Ctx& c, const std::string& text, int32_t max_w, double em_units) { return tidy_dots(c.pen.fit(text, max_w, em_units)); }

/// A line in the windows' face (dark ink on the clay), its left edge, middle or right edge at x (align 0, 1, 2), its baseline at `baseline` (canvas pixels)
void ink(const Ctx& c, const std::string& text, int32_t x, int32_t baseline, double em_units, int align) {
    if (text.empty()) return;
    const int32_t em = c.s.em_tenths(em_units);
    if (c.r.dialog_text_ready()) {
        const int32_t w = c.r.dialog_text_width(text, em);
        c.r.draw_dialog_text(text, align == 0 ? x : align == 1 ? x - w / 2 : x - w, baseline, window_art::kInk, em);
    } else {
        c.pen.draw(text, x, baseline, window_art::kInk, em_units, align);
    }
}

/// One AntSlot as the new page draws it: the animation agst301 in the color of the team, running since the screen was created, its first frame's box (the whole picture of the ant) put at (ux, uy) units
/// and drawn `a` times its own size (1: a pixel of the ant is a unit); `head` shows only the top of it (a ranked row)
void draw_ant(const Ctx& c, uint8_t team, double ux, double uy, double a, bool head) {
    const auto* anim = c.assets.find_animation("agst301");
    if (anim == nullptr || anim->subitems.empty()) return;
    int32_t bx = INT32_MAX, by = INT32_MAX;
    for (const auto& part : anim->subitems[0].frames) {
        bx = std::min(bx, static_cast<int32_t>(part.dx));
        by = std::min(by, static_cast<int32_t>(part.dy));
    }
    const size_t frame = Renderer::get_anim_subitem_by_time(*anim, static_cast<uint32_t>(c.m.clock_ms()));
    const auto& parts = anim->subitems[frame].frames;
    const auto edge = [&](double u) { return static_cast<int32_t>(std::lround(u * c.s.f)); };
    if (head) {
        const LayoutRect clip = c.s.rect(static_cast<int32_t>(std::lround(ux)), static_cast<int32_t>(std::lround(uy)), kAntW + 2, kAntHeadH);
        c.r.set_clip_rect(clip.x, clip.y, clip.w, clip.h);
    }
    c.r.set_hud_team(team);
    for (size_t k = parts.size(); k-- > 0;) {
        const assets::Sprite& sp = c.assets.get_sprite(parts[k].sprite_index);
        const double left = ux + (parts[k].dx - bx) * a;
        const double top = uy + (parts[k].dy - by) * a;
        const int32_t x0 = edge(left), y0 = edge(top);
        const int32_t w = std::max(1, edge(left + sp.width * a) - x0), h = std::max(1, edge(top + sp.height * a) - y0);
        c.r.draw_sprite_region(parts[k].sprite_index, x0, y0, w, h, 0, 0, static_cast<int32_t>(sp.width), static_cast<int32_t>(sp.height));
    }
    c.r.set_hud_team(0);
    if (head) c.r.clear_clip_rect();
}

/// The ants of a row at (ux, uy): one, or two side by side `pitch` units apart
void draw_team(const Ctx& c, const ScorecardModal::Row& row, double ux, double uy, double a, bool head) {
    const double pitch = (kAntW * a) + 2.0;
    draw_ant(c, row.first, ux, uy, a, head);
    if (row.has_second()) draw_ant(c, row.second, ux + pitch, uy, a, head);
}

/// A button made of a strip and a word: the wide page's small one (face 22 high, the word on the baseline 15 below its top), or the narrow page's big one (the word's baseline is given)
void draw_button(const Ctx& c, const LayoutRect& face, bool big, int32_t word_base, const char* word, Look look) {
    const char* st = state_of(look);
    c.sheet.strip(big ? "sc_btn" : "sc_sm", face, st);
    c.sheet.lettering(std::string(big ? "sc_bw_" : "sc_sw_") + word + "_" + st, face.x + face.w / 2, big ? word_base : face.y + 15);
}

/// Leave Game: the corner's button, or the big one of a narrow page; after the match of a lobby room "Back to the room" in its place and "Back to Ants & Uncles" beside it (a narrow page: under it)
void draw_leave(const Ctx& c, const LayoutRect& face, const LayoutRect& hub, bool choice, bool big, int32_t word_base, int32_t hub_word_base) {
    if (choice) {
        draw_button(c, face, big, word_base, "room", c.m.leave_look());
        draw_button(c, hub, big, hub_word_base, "hub", c.m.hub_look());
        return;
    }
    const char* st = state_of(c.m.leave_look());
    if (big) {
        c.sheet.strip("sc_btn", face, st);
        c.sheet.lettering(std::string("sc_leave_word_") + st, face.x + face.w / 2, word_base);
    } else {
        c.sheet.put(std::string("sc_leave_") + st, face.x, face.y);
    }
}

/// The link to the other page: "All stats" on the podium, "Back to results" on the second page
void draw_link(const Ctx& c, const LayoutRect& face) {
    draw_button(c, face, false, 0, c.m.page() == Page::Results ? "allstats" : "back", c.m.link_look());
}

/// A bar of the score in the colors of the ants (an alliance: one half each)
void draw_bar(const Ctx& c, const LayoutRect& bar, double part, const ScorecardModal::Row& row) {
    const LayoutRect r = c.s.rect(bar);
    c.r.fill_rect(r.x, r.y, r.w, r.h, kTrack);
    const int32_t fill = std::max<int32_t>(1, static_cast<int32_t>(std::lround(static_cast<double>(r.w) * std::min(1.0, std::max(0.0, part)))));
    const int32_t n = row.has_second() ? 2 : 1;
    for (int32_t j = 0; j < n; ++j) {
        const int32_t a = r.x + fill * j / n, b = r.x + fill * (j + 1) / n;
        c.r.fill_rect(a, r.y, std::max(1, b - a), r.h, kTeam[(j == 0 ? row.first : row.second) & 3]);
    }
}

uint64_t damage_of(const MatchReport& rep, const ScorecardModal::Row& row) {
    return static_cast<uint64_t>(rep.damage_done[row.first & 3]) + (row.has_second() ? rep.damage_done[row.second & 3] : 0u);
}

// ---- the podium ---------------------------------------------------------------------------------------------------------------------------------------------

const char* title_sprite_of(const ScorecardModal& m, bool big) {
    const int rank = m.viewer_rank();
    if (rank == 0) return big ? "sc_victory_26" : "sc_victory_22";
    if (rank > 0) return big ? "sc_defeat_26" : "sc_defeat_22";            // (a player that is not in the match: Game Over)
    return "sc_gameover_26";
}

/// What stands beside the title: who won, and how long it took (or where the local player came), in one line that is cut at the end of the winner's name when it is too long
std::string sentence_of(const Ctx& c, const Podium& p) {
    const ScorecardModal& m = c.m;
    const auto& rows = m.rows();
    if (rows.empty()) return std::string();
    const ScorecardModal::Row& win = rows[0];
    const int rank = m.viewer_rank();
    const bool is_187 = m.game_mode() == sim::GameMode::Kills187;
    std::string tail;
    if (rank > 0) tail = "   You came " + ordinal(rank + 1) + ".";
    else if (rank == 0 && m.report().ok) tail = "   " + clock_of(m.report().turns) + " played";
    const double em = p.narrow ? 11.5 : 12.5;
    const int32_t em_tenths = c.s.em_tenths(em);
    const UnitMeter meter(c.r, c.s.f);
    const auto width = [&](const std::string& t) { return c.r.dialog_text_ready() ? c.r.dialog_text_width(t, em_tenths) : meter.width(t, em);};
    const int32_t room = static_cast<int32_t>(std::lround(static_cast<double>(p.sentence_w) * c.s.f));
    if (is_187 && !m.headline().empty()) return fit_dialog(c.r, meter, m.headline(), std::max(0, room - width(tail)), em, em_tenths) + tail;
    const std::string verb = rank > 0 ? (win.has_second() ? " win." : " wins.") : (win.has_second() ? " win the match." : " wins the match.");
    const std::string fixed = verb + tail;
    return fit_dialog(c.r, meter, win.name, std::max(0, room - width(fixed)), em, em_tenths) + fixed;
}

void draw_honors(const Ctx& c, const Podium& p) {
    const MatchReport& rep = c.m.report();
    const bool is_187 = c.m.game_mode() == sim::GameMode::Kills187;
    struct Card {
        const char* label;
        const MatchReport::Honor* honor;
        std::string detail;
    };
    const Card cards[3] = {
        {"sc_k_mvp", &rep.mvp, std::to_string(rep.mvp.value / 10) + "% of the play"},
        {"sc_k_blood", &rep.first_blood, "at " + clock_of(rep.first_blood.turn)},
        is_187 ? Card{"sc_k_stand", &rep.last_stand, "out of ants at " + clock_of(rep.last_stand.turn)}
               : Card{"sc_k_raid", &rep.biggest_raid, grouped(rep.biggest_raid.value) + " points at " + clock_of(rep.biggest_raid.turn)},
    };
    for (size_t i = 0; i < 3; ++i) {
        const LayoutRect& w = p.honor[i];
        c.sheet.well(w);
        c.sheet.lettering(cards[i].label, w.x + w.w / 2, w.y + p.honor_label_base);
        const MatchReport::Honor& h = *cards[i].honor;
        if (!h.present) {
            c.pen.draw("nobody", c.X(w.x + w.w / 2), c.Y(w.y + p.honor_name_base), kDim, 11.5, 1);
            continue;
        }
        const std::string name = fit_pen(c, c.m.seat_name(h.seat), c.W(w.w - 8), 11.5);
        const double ant_scale = static_cast<double>(p.honor_ant_h) / static_cast<double>(kAntH);
        draw_ant(c, h.seat, w.x + w.w / 2.0 - kAntW * ant_scale / 2.0, w.y + p.honor_ant_y, ant_scale, false);
        c.pen.draw(name, c.X(w.x + w.w / 2), c.Y(w.y + p.honor_name_base), kCream, 11.5, 1);
        c.pen.draw(fit_pen(c, cards[i].detail, c.W(w.w - 6), 8.2), c.X(w.x + w.w / 2), c.Y(w.y + p.honor_detail_base), kDim, 8.2, 1);
    }
}

/// What the Play of the game says, in two lines: the seat's name and what follows it ("Bea" "'s bomb takes out"), then the rest ("6 ants")
struct PlayWords {
    std::string who, first, second;
};
PlayWords play_words(const Ctx& c, const MatchReport::Play& play) {
    const std::string who = c.m.seat_name(play.seat);
    const std::string count = std::to_string(play.count);
    switch (play.kind) {
        case MatchReport::PlayKind::Bomb: return {who, "'s bomb takes out", count + (play.count == 1 ? " ant" : " ants")};
        case MatchReport::PlayKind::Raid: return {who, "'s raid", "steals " + count + (play.count == 1 ? " point" : " points")};
        case MatchReport::PlayKind::Fire: return {who, "'s fire wall", "burns " + count + (play.count == 1 ? " ant" : " ants")};
        case MatchReport::PlayKind::Fight: return {who, " wins a fight", count + (play.count == 1 ? " kill" : " kills") + " in 10 seconds"};
    }
    return {who, std::string(), std::string()};
}

void draw_play(const Ctx& c, const Podium& p) {
    const MatchReport::Play& play = c.m.report().play;
    c.sheet.well(p.play);
    // the picture of the moment: the ants of the seat that made it, on a ground (the game keeps no picture of it)
    const LayoutRect pic = c.s.rect(p.play_picture);
    c.r.fill_rect(pic.x, pic.y, pic.w, pic.h, kPicture);
    const double a = static_cast<double>(p.play_picture.h) / static_cast<double>(kAntH);
    const double ant_w = kAntW * a;
    draw_ant(c, play.seat, p.play_picture.x + (p.play_picture.w - ant_w) / 2.0, p.play_picture.y, a, false);
    c.sheet.lettering("sc_k_play", p.play_text_x, p.play_label_base);
    const int32_t room = c.W(p.play.right() - 8 - p.play_text_x);
    const PlayWords words = play_words(c, play);
    const std::string when = "at " + clock_of(play.turn);
    const int32_t name_room = std::max(0, room - c.pen.width(words.first, 12.0));
    std::string who = words.who;
    if (c.pen.width(who, 12.0) > name_room && who.find(' ') != std::string::npos && c.pen.width(who.substr(0, who.find(' ')), 12.0) <= name_room) who = who.substr(0, who.find(' '));     // ("Bartholomew's fire wall", not "Bartholomew the Third of Antw...'s fire wall")
    std::array<std::string, 2> lines = {fit_pen(c, who, name_room, 12.0) + words.first, words.second};
    if (p.narrow) lines[1] += "  " + when;
    int32_t base = p.play_line_base;
    for (const std::string& line : lines) {
        c.pen.draw(fit_pen(c, line, room, 12.0), c.X(p.play_text_x), c.Y(base), kCream, 12.0, 0);
        base += p.play_pitch;
    }
    if (!p.narrow) c.pen.draw(when, c.X(p.play_text_x), c.Y(base), kDim, 10.0, 0);
    if (c.m.watch_available()) c.sheet.put(std::string("sc_watch_") + state_of(c.m.watch_look()), p.watch.x, p.watch.y);
}

void draw_podium(const Ctx& c) {
    const Podium& p = c.m.podium();
    const auto& rows = c.m.rows();
    const MatchReport& rep = c.m.report();
    const bool counted = rep.ok;
    const bool is_187 = c.m.game_mode() == sim::GameMode::Kills187;
    const std::string title = title_sprite_of(c.m, p.title_big);
    if (p.title_centered) c.sheet.centered(title, p.title_x, p.title_base);
    else c.sheet.lettering(title, p.title_x, p.title_base);
    ink(c, sentence_of(c, p), c.X(p.sentence_x), c.Y(p.sentence_base), p.narrow ? 11.5 : 12.5, p.narrow ? 1 : 0);
    if (rows.empty()) return;

    // the winner's well
    const ScorecardModal::Row& win = rows[0];
    c.sheet.well(p.winner_well);
    c.sheet.lettering(is_187 && c.m.result().standing < sim::MAX_PLAYERS ? "sc_k_colony" : "sc_k_winner", p.winner_label_x, p.winner_label_base);
    draw_team(c, win, p.ants_x, p.ants_y, 2.0, false);
    c.pen.draw(fit_pen(c, win.name, c.W(p.name_w), 17.0), c.X(p.name_x), c.Y(p.name_base), kCream, 17.0, 0);
    const double score_em = std::min(34.0, 34.0 * static_cast<double>(c.W(p.name_w) - c.W(44)) / std::max<double>(1.0, c.pen.width(win.numbers[0], 34.0)));
    c.pen.draw(win.numbers[0], c.X(p.name_x), c.Y(p.score_base), kGold, score_em, 0);
    c.pen.draw(is_187 ? "kills" : "score", c.X(p.name_x) + c.pen.width(win.numbers[0], score_em) + c.W(6), c.Y(p.score_base), kDim, 10.0, 0);
    std::vector<std::pair<std::string, std::string>> tiles;        // (value, label)
    if (is_187) {                                                  // (187: the score is the kills)
        tiles.push_back({win.numbers[1], "Lost"});
        tiles.push_back({win.numbers[2], "Left"});
    } else {                                                       // kills first, then deaths (the owner's order, as the history page has it)
        tiles.push_back({win.numbers[2], "Killed"});
        tiles.push_back({win.numbers[1], "Lost"});
        tiles.push_back({win.numbers[3], "Hatched"});
    }
    if (counted) tiles.push_back({grouped(static_cast<int64_t>(damage_of(rep, win))), "Damage"});
    const int32_t tile_room = p.tile_count > 1 ? c.W(p.tile[1].x - p.tile[0].x) - c.W(3) : c.W(p.name_w);          // (a number too long for its tile is set smaller)
    for (size_t i = 0; i < tiles.size() && i < p.tile_count; ++i) {
        const int32_t w = c.pen.width(tiles[i].first, 14.0);
        c.pen.draw(tiles[i].first, c.X(p.tile[i].x), c.Y(p.tile_base), kCream, w > tile_room && w > 0 ? 14.0 * tile_room / w : 14.0, 1);
        c.pen.draw(tiles[i].second, c.X(p.tile[i].x), c.Y(p.tile_label_base), kDim, 8.2, 1);
    }

    // the others, ranked, with a bar of the score
    if (p.other_count > 0) {
        c.sheet.lettering("sc_others", p.others_label_x, p.others_label_base);
        c.sheet.well(p.others_well);
        int32_t top = 0;
        for (const ScorecardModal::Row& r : rows) top = std::max(top, r.score_value);
        for (size_t i = 0; i < p.other_count && i + 1 < rows.size(); ++i) {
            const ScorecardModal::Row& r = rows[i + 1];
            const OtherRow& o = p.other[i];
            if (i > 0) {
                const LayoutRect hair = c.s.rect(o.box.x + 8, o.box.y, o.box.w - 18, 1);
                c.r.fill_rect(hair.x, hair.y, hair.w, std::max(1, hair.h), kDivider);
            }
            c.pen.draw(std::to_string(i + 2), c.X(o.rank_x), c.Y(o.rank_base), kDim, 13.0, 1);
            draw_team(c, r, o.head_x, o.head_y, 1.0, true);
            const int32_t score_w = c.pen.width(r.numbers[0], 16.0);
            const bool you = c.m.viewer_rank() == static_cast<int>(i) + 1;
            const std::string name = r.name + (you ? " (you)" : "");
            c.pen.draw(r.numbers[0], c.X(o.score_right), c.Y(o.score_base), kCream, 16.0, 2);
            c.pen.draw(fit_pen(c, name, c.X(o.name_right) - c.X(o.name_x) - score_w, 14.0), c.X(o.name_x), c.Y(o.name_base), kCream, 14.0, 0);
            draw_bar(c, o.bar, top > 0 ? static_cast<double>(std::max(0, r.score_value)) / static_cast<double>(top) : 0.0, r);
            const std::string small_numbers = is_187 ? "Lost " + r.numbers[1] + "   Left " + r.numbers[2]
                                                     : "Killed " + r.numbers[2] + "   Lost " + r.numbers[1] + "   Hatched " + r.numbers[3];
            std::string small = small_numbers;
            if (counted) small += "   Damage " + grouped(static_cast<int64_t>(damage_of(rep, r)));
            c.pen.draw(fit_pen(c, small, c.X(o.score_right) - c.X(o.name_x), 8.6), c.X(o.name_x), c.Y(o.small_base), kDim, 8.6, 0);
        }
    }
    if (counted && rep.play.present && p.play.w > 0) draw_play(c, p);
    if (counted && p.honors) draw_honors(c, p);
    if (p.link_shown) draw_link(c, p.link);
    draw_leave(c, p.leave, p.hub, p.choice, p.leave_big, p.leave_word_base, p.hub_word_base);
}

// ---- the waiting page ---------------------------------------------------------------------------------------------------------------------------------------

void draw_waiting(const Ctx& c) {
    const Waiting& w = c.m.waiting_layout();
    c.sheet.centered(w.title_big ? "sc_gameover_36" : "sc_gameover_26", w.title_cx, w.title_base);
    ink(c, "The match has ended. Adding up the scores...", c.X(w.title_cx), c.Y(w.sentence_base), w.narrow ? 13.0 : 15.0, 1);
    draw_leave(c, w.leave, w.hub, w.choice, w.leave_big, w.leave_word_base, w.hub_word_base);
}

// ---- the second page ----------------------------------------------------------------------------------------------------------------------------------------

void draw_stats(const Ctx& c) {
    const StatsPage& s = c.m.stats_layout();
    const MatchReport& rep = c.m.report();
    const bool is_187 = c.m.game_mode() == sim::GameMode::Kills187;
    const auto& rows = c.m.rows();
    std::vector<uint8_t> seats;                                            // allies stand next to each other
    std::vector<bool> new_row;                                             // the seat is the first of its row (a stronger line over it)
    for (const ScorecardModal::Row& r : rows) {
        seats.push_back(r.first);
        new_row.push_back(true);
        if (r.has_second()) {
            seats.push_back(r.second);
            new_row.push_back(false);
        }
    }
    // the numbers of each seat, in the order of the columns
    std::vector<std::array<int64_t, 8>> values(seats.size());
    for (size_t q = 0; q < seats.size(); ++q) {
        const size_t t = seats[q] & 3u;
        std::array<int64_t, 8> v{};
        if (is_187) v = {static_cast<int64_t>(rep.damage_done[t]), static_cast<int64_t>(rep.damage_taken[t]), rep.bombs_set[t], rep.fires_lit[t], rep.raids[t], rep.loot[t], 0, 0};
        else v = {static_cast<int64_t>(rep.damage_done[t]), static_cast<int64_t>(rep.damage_taken[t]), rep.food_trips[t], rep.food_points[t], rep.bombs_set[t], rep.fires_lit[t], rep.raids[t], rep.loot[t]};
        values[q] = v;
    }
    std::vector<int> best(s.columns, -1);                                  // the best of a column (more than none) is gold
    for (size_t i = 0; i < s.columns; ++i) {
        int64_t top = 0;
        for (size_t q = 0; q < seats.size(); ++q) {
            if (values[q][i] > top) {
                top = values[q][i];
                best[i] = static_cast<int>(q);
            }
        }
    }
    static const char* const kSubs[8] = {"sc_s_done", "sc_s_taken", "sc_s_trips", "sc_s_points", "sc_s_bombs", "sc_s_fires", "sc_s_raids", "sc_s_loot"};
    static const char* const kSubs187[6] = {"sc_s_done", "sc_s_taken", "sc_s_bombs", "sc_s_fires", "sc_s_raids", "sc_s_loot"};
    static const char* const kShort[8] = {"Dmg done", "Dmg taken", "Food trips", "Food pts", "Bombs", "Fires", "Raids", "Loot"};
    static const char* const kShort187[6] = {"Dmg done", "Dmg taken", "Bombs", "Fires", "Raids", "Loot"};
    if (!s.narrow) {
        c.sheet.centered("sc_matchstats_15", s.title_cx, s.title_base);
        static const char* const kGroups[3] = {"sc_g_damage", "sc_g_food", "sc_g_special"};
        for (size_t g = 0; g < s.groups; ++g) {
            c.sheet.lettering(kGroups[is_187 && g == 1 ? 2 : g], s.group_cx[g], s.group_base);
            const LayoutRect line = c.s.rect(s.group_line[g]);
            c.r.fill_rect(line.x, line.y, line.w, std::max(1, line.h), assets::ColorRGBA{31, 23, 51, 140});
        }
        for (size_t i = 0; i < s.columns; ++i) c.sheet.lettering(is_187 ? kSubs187[i] : kSubs[i], s.column_right[i], s.sub_base);
    }
    c.sheet.well(s.well);
    for (size_t q = 0; q < seats.size() && q < s.seat_count; ++q) {
        const StatsPage::Seat& seat = s.seat[q];
        if (q > 0) {
            const LayoutRect hair = c.s.rect(s.content.x + 12, seat.box.y, s.content.w - 26, 1);
            c.r.fill_rect(hair.x, hair.y, hair.w, std::max(1, hair.h), new_row[q] ? kRowDivider : kDivider);
        }
        ScorecardModal::Row single;
        single.first = seats[q];
        draw_team(c, single, seat.ant_x, seat.ant_y, 1.0, false);
        const double em = s.narrow ? 16.0 : 15.0;
        c.pen.draw(fit_pen(c, c.m.seat_name(seats[q]), c.W(seat.name_w), em), c.X(seat.name_x), c.Y(seat.name_base), kCream, em, 0);
        for (size_t i = 0; i < s.columns; ++i) {
            const std::string text = grouped(values[q][i]);
            const bool gold = best[i] == static_cast<int>(q);
            const int32_t room = c.W(s.number_w);
            const int32_t w = c.pen.width(text, 15.0);
            const double fit_em = w > room && w > 0 ? 15.0 * room / w : 15.0;
            c.pen.draw(text, c.X(seat.number_x[i]), c.Y(seat.number_base[i]), gold ? kGold : kCream, fit_em, s.narrow ? 1 : 2);
            if (s.narrow) c.pen.draw(is_187 ? kShort187[i] : kShort[i], c.X(seat.number_x[i]), c.Y(seat.label_base[i]), kDim, 8.6, 1);
        }
    }
    draw_link(c, s.link);
    draw_leave(c, s.leave, s.hub, s.choice, s.leave_big, s.leave_word_base, s.hub_word_base);
}

}  // namespace

// ---- the state of the new page ------------------------------------------------------------------------------------------------------------------------------

int ScorecardModal::viewer_rank() const noexcept {
    if (((result_.present_mask >> (local_player_id_ & 3u)) & 1u) == 0u) return -1;
    for (size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].first == local_player_id_ || (rows_[i].has_second() && rows_[i].second == local_player_id_)) return static_cast<int>(i);
    }
    return -1;
}

static LayoutRect canvas_rect_of(const ButtonRect& r) { return LayoutRect{r.x, r.y, r.w, r.h}; }

LayoutRect ScorecardModal::leave_rect() const noexcept { return canvas_rect_of(quit_.up_rect()); }
LayoutRect ScorecardModal::hub_rect() const noexcept { return canvas_rect_of(hub_.up_rect()); }
LayoutRect ScorecardModal::link_rect() const noexcept { return canvas_rect_of(link_.up_rect()); }
LayoutRect ScorecardModal::watch_rect() const noexcept { return canvas_rect_of(watch_.up_rect()); }

void ScorecardModal::show_page(Page page) {
    if (!new_look_ || page == page_) return;
    page_ = page;
    update_new_look();                                                // (which takes the second page back at once when there are no rows yet or no report that is ok)
}

// The new page (scorecard_layout.hpp) is there while the art has every piece of it and the canvas holds 360 units on its shorter side. Its layouts follow the canvas, the rows and what was counted
void ScorecardModal::update_new_look() {
    new_look_ = false;
    if (art_ != nullptr && canvas_w_ > 0 && canvas_h_ > 0 && window_art::has_results_page(*art_)) {
        scale_ = scorecard_layout::scale_for(canvas_w_, canvas_h_, output_);
        new_look_ = scale_.usable();
    }
    if (new_look_) {
        const bool rows_shown = phase_ == Phase::Rows;
        if (page_ == Page::Stats && !(rows_shown && report_.ok)) page_ = Page::Results;
        PodiumInput in;
        in.w = scale_.w;
        in.h = scale_.h;
        in.rows = !rows_.empty() ? rows_.size() : std::max<size_t>(1, result_.rows(local_player_id_).size());
        in.winner_ants = !rows_.empty() && rows_[0].has_second() ? 2 : 1;
        for (size_t i = 0; i + 1 < rows_.size() && i < in.other_ants.size(); ++i) in.other_ants[i] = rows_[i + 1].has_second() ? 2 : 1;
        in.report = rows_shown && report_.ok;
        in.play = in.report && report_.play.present;
        in.is_187 = mode_ == sim::GameMode::Kills187;
        const hud_art::Sprite* title = art_->sprite_at(art_->find_sprite(title_sprite_of(*this, in.w >= kNarrowBelow)));
        in.title_w = title != nullptr ? title->w - 2 * title->x : 0;
        in.choice = room_choice_;
        podium_ = compute_podium(in);
        waiting_ = compute_waiting(scale_.w, scale_.h, room_choice_);
        StatsInput st;
        st.w = scale_.w;
        st.h = scale_.h;
        st.seats = 0;
        for (const Row& r : rows_) st.seats += r.has_second() ? 2u : 1u;
        st.seats = std::max<size_t>(1, st.seats);
        st.is_187 = in.is_187;
        st.choice = room_choice_;
        stats_ = compute_stats(st);
    }
    update_button();
}

static void set_rect(ScreenButton& b, const LayoutRect& r) {
    const ButtonRect rect{r.x, r.y, r.w, r.h};
    if (b.up_rect() == rect && b.pressed_rect() == rect) return;
    b = ScreenButton(rect, rect);
}

// The new page's buttons stand where the layout of the page that shows puts them: Leave Game (or the two buttons of a room's match) from the first moment, and with a report that is ok the link to the other page and Watch
void ScorecardModal::update_new_buttons() {
    const bool rows_shown = phase_ == Phase::Rows;
    const bool two_pages = rows_shown && report_.ok;
    LayoutRect leave, hub, link, watch;
    if (!rows_shown) {
        leave = scale_.rect(waiting_.leave);
        hub = scale_.rect(waiting_.hub);
    } else if (page_ == Page::Stats) {
        leave = scale_.rect(stats_.leave);
        hub = scale_.rect(stats_.hub);
        link = scale_.rect(stats_.link);
    } else {
        leave = scale_.rect(podium_.leave);
        hub = scale_.rect(podium_.hub);
        if (two_pages) link = scale_.rect(podium_.link);
        if (two_pages && watch_available_ && report_.play.present && podium_.play.w > 0) watch = scale_.rect(podium_.watch);
    }
    set_rect(quit_, leave);
    set_rect(hub_, hub);
    set_rect(link_, link);
    set_rect(watch_, watch);
}

bool ScorecardModal::new_mouse_down(int32_t x, int32_t y) {
    if (quit_.on_press(x, y) || hub_.on_press(x, y) || watch_.on_press(x, y) || link_.on_press(x, y)) play_sfx(sim::SoundID::ButtonClick);
    return true;                                                   // (the screen takes every click)
}

bool ScorecardModal::new_mouse_up(int32_t x, int32_t y) {
    if (quit_.on_release(x, y)) {
        leave();
    } else if (hub_.on_release(x, y)) {
        if (on_back_to_hub_) on_back_to_hub_();
    } else if (watch_.on_release(x, y)) {
        if (on_watch_) on_watch_(report_.play.watch_turn(), report_.play.tile_x, report_.play.tile_y);
    } else if (link_.on_release(x, y)) {
        show_page(link_target());
    }
    return true;
}

void ScorecardModal::new_mouse_motion(int32_t x, int32_t y) {
    quit_.on_move(x, y);
    hub_.on_move(x, y);
    watch_.on_move(x, y);
    link_.on_move(x, y);
}

void ScorecardModal::render_new(IRenderer& renderer, const assets::AssetArchive& assets) {
    const hud_art::Art& art = *art_;
    renderer.set_art_scale(scale_.sheet);
    std::vector<hud_art::Draw> panel = hud_art::panel_draws(art, LayoutRect{0, 0, scale_.w, scale_.h});
    for (hud_art::Draw& d : panel) d.dst = scale_.rect(d.dst);
    hud_art::draw_all(renderer, art, panel);
    const Ctx c(renderer, art, scale_, assets, *this);
    if (phase_ == Phase::Waiting) draw_waiting(c);
    else if (page_ == Page::Stats) draw_stats(c);
    else draw_podium(c);
    renderer.set_art_scale(0);
}

}  // namespace ants::app
