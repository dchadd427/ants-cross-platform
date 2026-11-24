// The results screen in the new look, the model's part (include/ants_app/scorecard_layout.hpp, window_art::has_results_page; docs/VIEW_AND_HUD.md, "The new look of the results screen"): the unit of the page,
// the three layouts (the podium, the waiting page, the second page "Match stats") at every size of the list and at a sweep of others, and the art's pieces. The cases that draw are in
// test_scorecard_new_look.cpp.
//   [scale]    a unit is a whole number of device pixels at 640 x 480, 720p, 1080p, 1440p, an ultrawide and a phone held upright and sideways, with the numbers of the design
//   [podium]   at every one of those sizes, for one to four rows, with and without the counted numbers, with and without a Play of the game, in 187: nothing is cut, nothing overlaps, every row has its place
//   [waiting]  the waiting page and the second page at the same sizes
//   [sweep]    the same rules at a sweep of sizes (every width and height from a phone to an ultrawide)
//   [pieces]   the shipped art has every piece of the page, at the sizes that the layout counts on; a set that lacks one or has one of another size gives the original's page
// Usage: test_scorecard_new. Exit code 0 when every check passes.
#include "hud_art_fixtures.hpp"

#include "ants_app/scorecard.hpp"
#include "ants_app/scorecard_layout.hpp"
#include "ants_app/window_art.hpp"

namespace sl = ants::app::scorecard_layout;

namespace {

/// A screen of the list of sizes that the page is made for: the canvas that the game would draw for it and the window that shows it (its output scale is the arithmetic of the screenshot, CanvasLayout::fit)
struct Screen {
    const char* name;
    int32_t canvas_w, canvas_h;
    int32_t window_w, window_h;
    int32_t k;                     // the unit of the design at that size
    int32_t units_w, units_h;      // the page in units
};

const Screen kScreens[] = {
    {"640 x 480", 640, 480, 640, 480, 1, 640, 480},
    {"1280 x 720", 960, 540, 1280, 720, 2, 640, 360},
    {"1920 x 1080", 960, 540, 1920, 1080, 3, 640, 360},
    {"2560 x 1440", 960, 540, 2560, 1440, 4, 640, 360},
    {"3440 x 1440 (21:9)", 1260, 540, 3440, 1440, 4, 840, 360},
    {"phone held upright (1170 x 2532)", 390, 844, 1170, 2532, 3, 390, 844},
    {"phone held sideways (2532 x 1170)", 1200, 540, 2532, 1170, 3, 844, 379},
    {"640 x 480 at 150% (a window of 960 x 720)", 640, 480, 960, 720, 1, 960, 720},       // (a landscape picture under 520 units takes the next smaller unit)
};

double output_of(const Screen& s) { return CanvasLayout{s.canvas_w, s.canvas_h}.fit(s.window_w, s.window_h).scale; }

bool inside(const LayoutRect& r, int32_t w, int32_t h) { return r.x >= 0 && r.y >= 0 && r.right() <= w && r.bottom() <= h && r.w > 0 && r.h > 0; }

bool overlap(const LayoutRect& a, const LayoutRect& b) { return a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom(); }

bool contains(const LayoutRect& outer, const LayoutRect& in) { return in.x >= outer.x && in.y >= outer.y && in.right() <= outer.right() && in.bottom() <= outer.bottom(); }

/// A box of lettering is cut with one unit of air and its soft shadow: what must not touch is the ink, two units in from the box on every side
LayoutRect ink_of(const LayoutRect& r) { return LayoutRect{r.x + 2, r.y + 2, std::max(1, r.w - 4), std::max(1, r.h - 4)}; }

/// The rectangle that a lettering sprite takes when its alignment point is at (anchor_x, baseline)
LayoutRect letters(const hud_art::Art& art, const char* name, int32_t anchor_x, int32_t baseline) {
    const hud_art::Sprite* s = art.sprite_at(art.find_sprite(name));
    if (s == nullptr) return LayoutRect{};
    return LayoutRect{anchor_x - s->x, baseline - s->y, s->w, s->h};
}

/// The rectangle of a playful title centered on cx
LayoutRect centered_letters(const hud_art::Art& art, const char* name, int32_t cx, int32_t baseline) {
    const hud_art::Sprite* s = art.sprite_at(art.find_sprite(name));
    if (s == nullptr) return LayoutRect{};
    return LayoutRect{cx - s->w / 2, baseline - s->y, s->w, s->h};
}

/// The rectangle of the picture of a well round its ground: 4 more left of and above, 6 more right of and below (the last two of them are its soft shadow)
LayoutRect well_picture(const LayoutRect& g) { return LayoutRect{g.x - 4, g.y - 4, g.w + 10, g.h + 10}; }

/// The bezel of a well: the ground with 4 more all round (wells 8 apart touch)
LayoutRect bezel(const LayoutRect& g) { return LayoutRect{g.x - 4, g.y - 4, g.w + 8, g.h + 8}; }

/// The ink of a lettering sprite: the box of its pixels that are opaque enough to be seen (its box less the clear margins), in the sprite's own units
LayoutRect ink_in_sprite(const hud_art::Art& art, const char* name) {
    static DecodedImage sheet;
    static const hud_art::Art* sheet_of = nullptr;
    if (sheet_of != &art) {
        sheet_of = art.sheet_pixels(1, sheet) ? &art : nullptr;
    }
    const hud_art::Sprite* s = art.sprite_at(art.find_sprite(name));
    if (sheet_of == nullptr || s == nullptr) return LayoutRect{0, 0, s != nullptr ? s->w : 0, s != nullptr ? s->h : 0};
    int32_t x0 = s->w, y0 = s->h, x1 = -1, y1 = -1;
    for (int32_t y = 0; y < s->h; ++y) {
        for (int32_t x = 0; x < s->w; ++x) {
            if (sheet.rgba[(static_cast<size_t>(s->sheet_y + y) * static_cast<size_t>(sheet.width) + static_cast<size_t>(s->sheet_x + x)) * 4u + 3u] < 140) continue;
            x0 = std::min(x0, x);
            y0 = std::min(y0, y);
            x1 = std::max(x1, x);
            y1 = std::max(y1, y);
        }
    }
    return x1 < 0 ? LayoutRect{} : LayoutRect{x0, y0, x1 - x0 + 1, y1 - y0 + 1};
}

/// Where the ink of a lettering sprite lands when it is put as the page puts it (its alignment point at (anchor_x, baseline), or its middle at cx)
LayoutRect ink_letters(const hud_art::Art& art, const char* name, int32_t anchor_x, int32_t baseline) {
    const LayoutRect box = letters(art, name, anchor_x, baseline);
    const LayoutRect ink = ink_in_sprite(art, name);
    return LayoutRect{box.x + ink.x, box.y + ink.y, ink.w, ink.h};
}
LayoutRect ink_centered(const hud_art::Art& art, const char* name, int32_t cx, int32_t baseline) {
    const LayoutRect box = centered_letters(art, name, cx, baseline);
    const LayoutRect ink = ink_in_sprite(art, name);
    return LayoutRect{box.x + ink.x, box.y + ink.y, ink.w, ink.h};
}

/// The width of the title's lettering, as the screen measures it (the sprite without its two margins)
int32_t title_width(const hud_art::Art& art, const char* name) {
    const hud_art::Sprite* s = art.sprite_at(art.find_sprite(name));
    return s != nullptr ? s->w - 2 * s->x : 0;
}

/// What a case of the podium holds
struct Case {
    int32_t w{0}, h{0};
    size_t rows{1};
    std::array<int32_t, 4> ants{{1, 1, 1, 1}};      // the ants of each row, the winner's first
    bool report{false};
    bool play{false};
    bool is_187{false};
};

std::string describe(const Case& c) {
    std::string out = std::to_string(c.w) + " x " + std::to_string(c.h) + ", " + std::to_string(c.rows) + " rows (";
    for (size_t i = 0; i < c.rows; ++i) out += std::to_string(c.ants[i]);
    return out + " ants)" + (c.report ? (c.play ? ", counted with a play" : ", counted") : ", not counted") + (c.is_187 ? ", 187" : "");
}

sl::PodiumInput input_of(const hud_art::Art& art, const Case& c) {
    sl::PodiumInput in;
    in.w = c.w;
    in.h = c.h;
    in.rows = c.rows;
    in.winner_ants = c.ants[0];
    for (size_t i = 0; i < 3; ++i) in.other_ants[i] = c.ants[i + 1];
    in.report = c.report;
    in.play = c.play;
    in.is_187 = c.is_187;
    in.title_w = title_width(art, c.w >= sl::kNarrowBelow ? "sc_victory_26" : "sc_victory_22");
    return in;
}

/// The rules that every set of boxes keeps: all of them are on the page, none lies over another (a box that is inside another one by design is listed in `inner`)
void check_parts(const std::vector<sl::Part>& parts, int32_t w, int32_t h, const std::string& at) {
    for (const auto& p : parts) check(inside(p.rect, w, h), at + ": " + p.name + " is on the page " + str(p.rect));
    for (size_t i = 0; i < parts.size(); ++i) {
        for (size_t j = i + 1; j < parts.size(); ++j) {
            const bool inner = (parts[i].name == "Watch" && parts[j].name == "the play of the game") || (parts[j].name == "Watch" && parts[i].name == "the play of the game");
            if (inner) continue;
            const auto lettering = [](const std::string& name) { return name == "the title" || name == "the sentence"; };
            check(!overlap(lettering(parts[i].name) ? ink_of(parts[i].rect) : parts[i].rect, lettering(parts[j].name) ? ink_of(parts[j].rect) : parts[j].rect),
                  at + ": " + parts[i].name + " " + str(parts[i].rect) + " and " + parts[j].name + " " + str(parts[j].rect) + " do not overlap");
        }
    }
}

/// Every rule of a podium: what is on the page is on the page, in its order and apart
void check_podium(const hud_art::Art& art, const Case& c) {
    const std::string at = describe(c);
    const sl::PodiumInput in = input_of(art, c);
    const sl::Podium p = sl::compute_podium(in);
    check(p.ok && p.narrow == (c.w < sl::kNarrowBelow), at + ": the podium is made, narrow below 520 units");
    if (!p.ok) return;
    check_parts(p.parts, c.w, c.h, at);

    // the lettering of the title, the labels and the words, where the art puts it
    const char* title = c.w >= sl::kNarrowBelow ? "sc_victory_26" : "sc_victory_22";
    const LayoutRect title_rect = p.title_centered ? ink_centered(art, title, p.title_x, p.title_base) : ink_letters(art, title, p.title_x, p.title_base);
    check(inside(title_rect, c.w, c.h), at + ": the title is on the page " + str(title_rect));
    check(p.sentence_w > 80, at + ": the sentence has room (" + std::to_string(p.sentence_w) + ")");
    if (!p.narrow) check(p.sentence_x >= title_rect.right() && p.title_x >= p.content.x, at + ": the sentence stands after the title");
    else check(p.sentence_base - 11 >= title_rect.bottom() - 2, at + ": the sentence stands under the title");
    check(!overlap(title_rect, bezel(p.winner_well)), at + ": the title is over the winner's well " + str(title_rect));
    check(!overlap(title_rect, LayoutRect{p.leave.x, p.leave.y, p.leave.w + 3, p.leave.h + 4}), at + ": the title is over Leave Game");
    if (p.tabs) check(!overlap(title_rect, LayoutRect{p.tab_results.x, p.tab_results.y, p.tab_results.w + 3, p.tab_results.h + 4}) && !overlap(title_rect, LayoutRect{p.tab_stats.x, p.tab_stats.y, p.tab_stats.w + 3, p.tab_stats.h + 4}), at + ": the title is over a tab");
    const LayoutRect label = ink_letters(art, c.is_187 ? "sc_k_colony" : "sc_k_winner", p.winner_label_x, p.winner_label_base);
    check(contains(p.winner_well, label), at + ": the winner's label stands in its well " + str(label));

    // the winner's well: the ants at double size, then the name, the score, the tiles
    const int32_t ants = std::max(1, c.ants[0]);
    check(p.ants_x >= p.winner_well.x && p.ants_y >= p.winner_well.y && p.ants_y + sl::kAntH * 2 <= p.winner_well.bottom(), at + ": the winner's ants are in the well");
    check(p.ants_x + ants * (sl::kAntW * 2 + 2) <= p.name_x, at + ": the name stands right of the ants");
    check(p.name_w >= 60 && p.name_x + p.name_w <= p.winner_well.right(), at + ": the name has room (" + std::to_string(p.name_w) + ")");
    check(p.name_base > p.winner_label_base && p.score_base > p.name_base && p.tile_base > p.score_base && p.tile_label_base > p.tile_base, at + ": the name, the score and the tiles are in order down the well");
    check(p.tile_label_base + 3 <= p.winner_well.bottom() && p.score_base <= p.winner_well.bottom(), at + ": the tiles are in the well");
    check(p.tile_count == static_cast<size_t>((c.is_187 ? 2 : 3) + (c.report ? 1 : 0)), at + ": a tile for every number (" + std::to_string(p.tile_count) + ")");
    for (size_t i = 0; i < p.tile_count; ++i) {
        check(p.tile[i].x >= p.name_x && p.tile[i].x <= p.winner_well.right() - 8, at + ": tile " + std::to_string(i) + " is in the well");
        if (i > 0) check(p.tile[i].x - p.tile[i - 1].x >= 34, at + ": tiles " + std::to_string(i - 1) + " and " + std::to_string(i) + " have room for their numbers (" + std::to_string(p.tile[i].x - p.tile[i - 1].x) + ")");
    }

    // the others: ranked rows in a well, in order, each with a name's room and a bar
    check(p.other_count == std::min<size_t>(c.rows, 4) - 1, at + ": a row for every other (" + std::to_string(p.other_count) + ")");
    if (p.other_count > 0) {
        const LayoutRect others_label = ink_letters(art, "sc_others", p.others_label_x, p.others_label_base);
        check(inside(others_label, c.w, c.h) && others_label.bottom() <= p.others_well.y - 4 + 1, at + ": the others' label is over its well " + str(others_label));
        check(!overlap(others_label, bezel(p.winner_well)) && !overlap(others_label, title_rect), at + ": the others' label is clear of the winner's well and of the title");
        check(contains(p.others_well, p.other[0].box) && p.others_well.right() <= p.content.right() + 2, at + ": the others' well holds the rows");
        for (size_t i = 0; i < p.other_count; ++i) {
            const sl::OtherRow& r = p.other[i];
            const std::string row = at + ": the others' row " + std::to_string(i + 1);
            check(contains(p.others_well, r.box) && r.box.h >= 38, row + " is in the well and tall enough (" + str(r.box) + ")");
            if (i > 0) check(r.box.y >= p.other[i - 1].box.bottom(), row + " is under the one before it");
            check(r.rank_x < r.head_x && r.head_x + std::max(1, c.ants[i + 1]) * (sl::kAntW + 2) <= r.name_x, row + " has the rank, the heads and then the name");
            check(r.name_right - r.name_x >= 70 && r.name_right <= r.score_right && r.score_right <= r.box.right(), row + " has room for its name (" + std::to_string(r.name_right - r.name_x) + ") before its score");
            check(r.name_base < r.bar.y && r.bar.bottom() < r.small_base && r.small_base <= r.box.bottom() && r.bar.right() <= r.box.right() && r.bar.x == r.name_x, row + ": the name, the bar and the small numbers are in order, in the row");
            check(r.rank_base <= r.box.bottom() && r.score_base < r.bar.y, row + ": the rank and the score are in the row");
        }
        check(p.others_well.right() <= p.winner_well.x + 100000 && (p.narrow ? p.others_well.y >= p.winner_well.bottom() : p.others_well.x >= p.winner_well.right() + 8), at + ": the others' well is " + (p.narrow ? "under" : "beside") + " the winner's");
    }

    // the Play of the game, the honors and the tabs: with the counted numbers only
    check(p.tabs == c.report && (!p.tabs || (p.tab_results.right() + 3 < p.tab_stats.x && p.tab_stats.right() + 3 <= p.leave.x + (p.narrow ? 100000 : 0))), at + ": the two tabs are there when the match was counted, side by side");
    check(p.leave_big == p.narrow && p.leave.w >= (p.narrow ? 200 : 99) && p.leave.h == (p.narrow ? sl::kBigFaceH : sl::kLeaveFaceH), at + ": Leave Game is the corner's button, or the big one of a narrow page");
    if (p.narrow) check(p.leave.bottom() + 11 <= c.h && p.leave.y > p.winner_well.bottom(), at + ": the big button is at the bottom, with room for its shadow");
    if (!c.report) {
        check(p.play.w == 0 && !p.honors, at + ": nothing was counted: no play and no honors");
    } else {
        if (p.honors) {
            for (size_t i = 0; i < 3; ++i) {
                const LayoutRect& hw = p.honor[i];
                check(hw.w >= 80 && hw.h >= 60 && inside(well_picture(hw), c.w, c.h), at + ": honor " + std::to_string(i) + " is a card on the page (" + str(hw) + ")");
                check(hw.y >= std::max(p.winner_well.bottom(), p.others_well.bottom()) + 6, at + ": honor " + std::to_string(i) + " is under the wells");
                if (i > 0) check(hw.x >= p.honor[i - 1].right() + 8 && hw.y == p.honor[i - 1].y && hw.w == p.honor[i - 1].w, at + ": the honors stand side by side, as wide as one another");
                check(p.honor_label_base < p.honor_ant_y && p.honor_ant_h >= 24 && p.honor_ant_y + p.honor_ant_h <= p.honor_name_base - 4 && p.honor_name_base < p.honor_detail_base && p.honor_detail_base + 3 <= hw.h, at + ": a card holds the label, an ant, the name and the detail, in that order");
                const LayoutRect honor_label = ink_letters(art, i == 0 ? "sc_k_mvp" : i == 1 ? "sc_k_blood" : (c.is_187 ? "sc_k_stand" : "sc_k_raid"), hw.x + hw.w / 2, hw.y + p.honor_label_base);
                check(contains(hw, honor_label), at + ": the label of honor " + std::to_string(i) + " is in its card " + str(honor_label));
            }
            check(p.honor[0].x >= p.content.x && p.honor[2].right() + 6 <= p.content.right() + 4, at + ": the honors stand inside the column");
        }
        if (c.play && p.play.w == 0) {
            check(p.narrow, at + ": only a narrow page that is too short for it has no play");
        } else if (c.play) {
            check(p.play.w >= 200 && inside(well_picture(p.play), c.w, c.h), at + ": the play of the game is a card on the page (" + str(p.play) + ")");
            check(contains(p.play, p.play_picture) && p.play_picture.w * 2 >= p.play_picture.h * 3 - 4, at + ": its picture is in the card, 3 to 2");
            check(p.play_text_x >= p.play_picture.right() + 8, at + ": its words stand right of the picture");
            const LayoutRect watch = LayoutRect{p.watch.x, p.watch.y, sl::kWatchW, sl::kWatchH};
            check(contains(p.play, p.watch) && p.watch.w == sl::kWatchFaceW && p.watch.h == sl::kWatchFaceH, at + ": Watch is in the card " + str(watch));
            check(p.watch.x >= p.play_text_x, at + ": Watch is right of the picture");
            const LayoutRect play_label = ink_letters(art, "sc_k_play", p.play_text_x, p.play_label_base);
            check(contains(p.play, play_label) && p.play_line_base > p.play_label_base, at + ": the label of the play is in its card " + str(play_label));
            check(p.play_line_base + p.play_pitch + 2 <= p.watch.y || p.play_line_base + p.play_pitch * 2 < p.watch.y + 4 || p.narrow, at + ": the sentence of the play is above Watch");
            check(p.play_line_base + p.play_pitch <= p.play.bottom(), at + ": two lines of the play are in the card");
            if (p.honors) check(p.honor[0].x >= p.play.right() + 8 || p.honor[0].y >= p.play.bottom() + 6, at + ": the honors do not lie over the play");
        } else {
            check(p.play.w == 0, at + ": no play, no card");
        }
    }
}

/// The waiting page and the second page at one size
void check_other_pages(const hud_art::Art& art, int32_t w, int32_t h, const std::string& at_size) {
    const sl::Waiting wt = sl::compute_waiting(w, h);
    const std::string at = at_size + ", waiting";
    check(wt.ok && wt.narrow == (w < sl::kNarrowBelow), at + ": the waiting page is made");
    if (wt.ok) {
        check_parts(wt.parts, w, h, at);
        const LayoutRect title = centered_letters(art, wt.title_big ? "sc_gameover_36" : "sc_gameover_26", wt.title_cx, wt.title_base);
        check(inside(title, w, h) && title.bottom() < wt.sentence_base - 4, at + ": \"Game Over\" is on the page, above its line " + str(title));
        check(wt.sentence_w > 150 && wt.sentence_base + 8 < h, at + ": the line has room");
        check(wt.leave_big == wt.narrow && wt.leave.w >= 99, at + ": Leave Game is there from the first moment");
        if (wt.narrow) check(wt.sentence_base + 8 < wt.leave.y, at + ": the big button is under the line");
    }
    for (const bool is_187 : {false, true}) {
        for (size_t seats = 1; seats <= 4; ++seats) {
            sl::StatsInput in;
            in.w = w;
            in.h = h;
            in.seats = seats;
            in.is_187 = is_187;
            const sl::StatsPage s = sl::compute_stats(in);
            const std::string sat = at_size + ", match stats, " + std::to_string(seats) + " seats" + (is_187 ? ", 187" : "");
            check(s.ok && s.seat_count == seats && s.columns == (is_187 ? 6u : 8u) && s.groups == (is_187 ? 2u : 3u), sat + ": the page is made (" + std::to_string(s.columns) + " columns)");
            if (!s.ok) continue;
            check_parts(s.parts, w, h, sat);
            check(s.tab_results.right() + 3 < s.tab_stats.x && s.leave_big == s.narrow, sat + ": the tabs and Leave Game are there");
            check(contains(LayoutRect{0, 0, w, h}, well_picture(s.well)), sat + ": the well is on the page " + str(s.well));
            for (size_t q = 0; q < seats; ++q) {
                const sl::StatsPage::Seat& seat = s.seat[q];
                const std::string row = sat + ": seat " + std::to_string(q);
                check(contains(s.well, seat.box) && (q == 0 || seat.box.y >= s.seat[q - 1].box.bottom()), row + " is in the well, under the one before it");
                check(seat.ant_x >= seat.box.x && seat.ant_y + sl::kAntH <= seat.box.bottom() + (s.narrow ? 0 : 4) && seat.name_x > seat.ant_x + sl::kAntW, row + " has its ant, then its name");
                check(seat.name_w >= (s.narrow ? 100 : 58), row + " has room for its name (" + std::to_string(seat.name_w) + ")");
                for (size_t i = 0; i < s.columns; ++i) {
                    const int32_t half = s.number_w / 2;
                    const int32_t left = s.narrow ? seat.number_x[i] - half : seat.number_x[i] - s.number_w;
                    const int32_t right = s.narrow ? seat.number_x[i] + half : seat.number_x[i];
                    check(left >= seat.box.x + 2 && right <= seat.box.right() && seat.number_base[i] <= seat.box.bottom() && seat.number_base[i] >= seat.box.y + 8, row + " number " + std::to_string(i) + " is in the row");
                    if (i > 0) check(left >= (s.narrow && (i % 4) == 0 ? 0 : (s.narrow ? seat.number_x[i - 1] + half : seat.number_x[i - 1])) - (s.narrow ? 2 : 0), row + " number " + std::to_string(i) + " does not touch the one before");
                    if (!s.narrow) check(left >= seat.name_x + seat.name_w - 2 || i > 0, row + " number " + std::to_string(i) + " is right of the name's room");
                    if (s.narrow) check(seat.label_base[i] > seat.number_base[i] && seat.label_base[i] + 2 <= seat.box.bottom(), row + " number " + std::to_string(i) + " has its label under it");
                }
            }
            if (!s.narrow) {
                const LayoutRect title = ink_centered(art, "sc_matchstats_15", s.title_cx, s.title_base);
                check(inside(title, w, h) && s.group_base < s.well.y, sat + ": the title and the headings are on the page " + str(title));
                static const char* const kGroupWords[3] = {"sc_g_damage", "sc_g_food", "sc_g_special"};
                for (size_t g = 0; g < s.groups; ++g) {
                    check(s.group_line[g].w > 0 && s.group_line[g].y < s.sub_base && s.group_line[g].y > s.group_base - 2, sat + ": group " + std::to_string(g) + " has its line under its heading");
                    const LayoutRect word = ink_letters(art, kGroupWords[is_187 && g == 1 ? 2 : g], s.group_cx[g], s.group_base);
                    check(word.x >= s.group_line[g].x - 2 && word.right() <= s.group_line[g].right() + 2 && word.bottom() <= s.group_line[g].y + 1, sat + ": the heading of group " + std::to_string(g) + " is over its line " + str(word));
                    if (g > 0) check(s.group_line[g].x >= s.group_line[g - 1].right(), sat + ": the groups are apart");
                }
                check(title.right() <= s.group_line[0].x && !overlap(title, ink_letters(art, kGroupWords[0], s.group_cx[0], s.group_base)), sat + ": the title is left of the groups " + str(title));
                check(s.sub_base < s.well.y - 4, sat + ": the sub headings are over the well");
                for (size_t i = 0; i < s.columns; ++i) {
                    static const char* const kSubWords[8] = {"sc_s_done", "sc_s_taken", "sc_s_trips", "sc_s_points", "sc_s_bombs", "sc_s_fires", "sc_s_raids", "sc_s_loot"};
                    static const char* const kSubWords187[6] = {"sc_s_done", "sc_s_taken", "sc_s_bombs", "sc_s_fires", "sc_s_raids", "sc_s_loot"};
                    const LayoutRect sub = ink_letters(art, is_187 ? kSubWords187[i] : kSubWords[i], s.column_right[i], s.sub_base);
                    check(inside(sub, w, h) && sub.bottom() <= s.well.y - 4 + 1, sat + ": sub heading " + std::to_string(i) + " is over the well " + str(sub));
                    if (i > 0) {
                        const LayoutRect before = ink_letters(art, is_187 ? kSubWords187[i - 1] : kSubWords[i - 1], s.column_right[i - 1], s.sub_base);
                        check(!overlap(sub, before), sat + ": sub headings " + std::to_string(i - 1) + " and " + std::to_string(i) + " do not touch " + str(before) + " " + str(sub));
                    }
                }
            }
        }
    }
}

void test_scale() {
    group("scale", "a unit is a whole number of device pixels, and the picture holds at least 360 of them on its shorter side");
    for (const Screen& s : kScreens) {
        const double o = output_of(s);
        const sl::Scale sc = sl::scale_for(s.canvas_w, s.canvas_h, o);
        const std::string at = std::string(s.name) + ": ";
        check(sc.k == s.k, at + "a unit is " + std::to_string(s.k) + " device pixels (" + std::to_string(sc.k) + ")");
        check(std::fabs(sc.f * o - static_cast<double>(sc.k)) < 1e-9, at + "a unit is whole device pixels: " + std::to_string(sc.f * o));
        check(sc.w == s.units_w && sc.h == s.units_h, at + "the page is " + std::to_string(s.units_w) + " x " + std::to_string(s.units_h) + " units (" + std::to_string(sc.w) + " x " + std::to_string(sc.h) + ")");
        check(std::min(sc.w, sc.h) >= sl::kMinShortSide && sc.usable(), at + "the shorter side holds 360 units");
        check(sc.sheet >= 1 && sc.sheet <= 3 && sc.sheet == std::min(sc.k, 3), at + "the sheet is the unit's, 3 at most (" + std::to_string(sc.sheet) + ")");
        check(sc.x(sc.w) == s.canvas_w && sc.y(sc.h) == s.canvas_h && sc.x(0) == 0, at + "the page's far edges are the canvas's: the panel covers it all");
        int32_t before = -1;
        bool monotonic = true;
        for (int32_t u = 0; u <= sc.w; ++u) {
            monotonic = monotonic && sc.x(u) > before;
            before = sc.x(u);
        }
        check(monotonic || sc.f < 1.0, at + "every unit edge falls on a pixel of its own (no unit has no width, while a unit is a canvas pixel or more)");
        bool never_back = true;
        for (int32_t u = 1; u <= sc.w; ++u) never_back = never_back && sc.x(u) >= sc.x(u - 1);
        check(never_back, at + "the edges run left to right");
        const LayoutRect r = sc.rect(10, 20, 30, 40);
        check(sc.rect(10, 20, 15, 40).right() == sc.rect(25, 20, 15, 40).x, at + "neighbors share an edge (" + str(r) + ")");
        check(s.canvas_w < s.canvas_h || sc.w >= sl::kNarrowBelow, at + "a landscape picture is never narrow");
    }
    const sl::Scale odd = sl::scale_for(1000, 540, 4.0 / 3.0);              // (a canvas that is not a whole number of units: the page still ends at the canvas's last pixel)
    check(odd.k == 2 && odd.x(odd.w) == 1000 && odd.y(odd.h) == 540 && odd.rect(0, 0, odd.w, odd.h) == (LayoutRect{0, 0, 1000, 540}), "a canvas of 1000 x 540 is not whole units wide: the last unit edge is the canvas's");
    const sl::Scale tiny = sl::scale_for(640, 480, 0.5);
    check(!tiny.usable() && tiny.k == 1, "a window of 320 x 240 holds 240 units: it keeps the original's page");
    check(sl::scale_for(960, 540, 0.0).k == 1 && sl::scale_for(960, 540, -3.0).k == 1, "an output that is not a number is a pixel to a pixel");
    const sl::Scale four = sl::scale_for(960, 540, 2560.0 / 960.0);
    check(four.k == 4 && four.sheet == 3, "a unit of 4 draws the 3x sheet");
    check(sl::scale_for(640, 480, 0.75).k == 1 && !sl::scale_for(640, 480, 0.75).usable(), "a 4:3 window of 480 x 360 is too small for the page (480 x 360 units: neither two columns nor a tall column): the original's page");
    check(sl::scale_for(640, 480, 0.9).usable() && sl::scale_for(390, 844, 1.0).usable(), "...a 4:3 window of 576 x 432 is two columns, a phone is a tall column");
    const sl::Scale at43 = sl::scale_for(640, 480, 1.5);
    check(at43.k == 1 && at43.w == 960 && at43.h == 720 && !sl::compute_podium(sl::PodiumInput{at43.w, at43.h}).narrow, "the 4:3 canvas at 1.5 is 960 x 720 units at 1: a page of two columns, not the stacked one");
}

void test_podium(const hud_art::Art& art) {
    group("podium", "at every size of the list the podium holds every row, with nothing cut and nothing over anything");
    for (const Screen& s : kScreens) {
        const sl::Scale sc = sl::scale_for(s.canvas_w, s.canvas_h, output_of(s));
        for (size_t rows = 1; rows <= 4; ++rows) {
            for (int32_t mask = 0; mask < (1 << rows); ++mask) {                       // (which rows are an alliance of two)
                Case c;
                c.w = sc.w;
                c.h = sc.h;
                c.rows = rows;
                for (size_t i = 0; i < rows; ++i) c.ants[i] = ((mask >> i) & 1) != 0 ? 2 : 1;
                for (const int32_t state : {0, 1, 2}) {
                    c.report = state > 0;
                    c.play = state == 2;
                    for (const bool is_187 : {false, true}) {
                        c.is_187 = is_187;
                        check_podium(art, c);
                    }
                }
            }
        }
        check_other_pages(art, sc.w, sc.h, s.name);
    }
    // what each size gets
    const sl::Scale wide = sl::scale_for(960, 540, 2.0);
    sl::PodiumInput in;
    in.w = wide.w;
    in.h = wide.h;
    in.rows = 3;
    in.report = true;
    in.play = true;
    in.title_w = title_width(art, "sc_victory_26");
    const sl::Podium full = sl::compute_podium(in);
    check(!full.narrow && !full.leave_big && full.leave.w == 99 && full.leave.h == 22 && full.honors && full.play.w > 0 && full.tabs, "720p, three rows, counted: the corner's button (99 x 22), the tabs, the play and the three honors");
    check(full.others_well.x > full.winner_well.right() && full.other_count == 2, "...the others are ranked beside the winner");
    in.report = false;
    in.play = false;
    const sl::Podium plain = sl::compute_podium(in);
    check(!plain.tabs && !plain.honors && plain.play.w == 0 && plain.tile_count == 3, "...a match that was not counted has no tabs, no honors, no play and no damage tile");
    in.rows = 1;
    const sl::Podium alone = sl::compute_podium(in);
    check(alone.other_count == 0 && alone.others_well.w == 0 && alone.winner_well.w > full.winner_well.w, "one row alone: no well of others, and the winner's well takes the width");
    const sl::Podium phone = sl::compute_podium([&] { sl::PodiumInput p = in; p.w = 390; p.h = 844; p.rows = 4; p.report = true; p.play = true; p.title_w = title_width(art, "sc_victory_22"); return p; }());
    check(phone.narrow && phone.leave_big && phone.leave.w > 300 && phone.tabs && phone.honors && phone.play.w > 0, "a phone held upright, four rows, counted: one column, the big button, the tabs, the play and the honors");
    const sl::Podium empty = sl::compute_podium(sl::PodiumInput{0, 0});
    check(!empty.ok, "no size, no page");
    sl::PodiumInput many = in;
    many.rows = 9;
    check(sl::compute_podium(many).other_count == 3, "more rows than there are teams are four");
    const sl::Podium at519 = sl::compute_podium(sl::PodiumInput{519, 900});
    const sl::Podium at520 = sl::compute_podium(sl::PodiumInput{520, 900});
    check(at519.narrow && !at520.narrow, "519 units wide stack the page in one column, 520 are two columns");
    // the page is centered in its height when it has room to spare, and starts at the top when it does not
    const sl::Podium tall = sl::compute_podium(sl::PodiumInput{640, 540, 2});
    const sl::Podium short_ = sl::compute_podium(sl::PodiumInput{640, 360, 2});
    check(tall.title_base > short_.title_base && tall.title_base - short_.title_base <= 60, "a page with room to spare is centered in its height (by 60 units at most)");
}

void test_waiting_and_stats(const hud_art::Art& art) {
    group("waiting", "the waiting page and the second page at every size of the list");
    for (const Screen& s : kScreens) {
        const sl::Scale sc = sl::scale_for(s.canvas_w, s.canvas_h, output_of(s));
        check(sl::compute_waiting(sc.w, sc.h).ok && sl::compute_stats(sl::StatsInput{sc.w, sc.h, 4, false}).ok, std::string(s.name) + ": both pages are made");
    }
    check(!sl::compute_waiting(0, 0).ok && !sl::compute_stats(sl::StatsInput{0, 0, 2, false}).ok, "no size, no page");
    const sl::Waiting centered = sl::compute_waiting(640, 360);
    check(centered.title_cx == 320 && centered.title_base < 360 / 2 + 10 && centered.sentence_base > centered.title_base, "the waiting page's \"Game Over\" is in the middle of the page, its line under it");
    const sl::StatsPage eight = sl::compute_stats(sl::StatsInput{640, 360, 4, false});
    const sl::StatsPage six = sl::compute_stats(sl::StatsInput{640, 360, 4, true});
    check(eight.columns == 8 && six.columns == 6 && eight.groups == 3 && six.groups == 2, "the second page has eight numbers in three groups, and the 187 page six in two (it has no food)");
    check(eight.column_right[7] == six.column_right[5] && eight.column_right[0] < eight.column_right[7], "the last column is at the same place, the columns run left to right");
    (void)art;
}

void test_sweep(const hud_art::Art& art) {
    group("sweep", "the same rules at every size from a phone to an ultrawide");
    int64_t cases = 0;
    int64_t bad = 0;
    for (int32_t w = 360; w <= 1100; w += (w < 560 ? 20 : 60)) {
        for (int32_t h = 360; h <= 1000; h += (h < 560 ? 20 : 60)) {
            const sl::Scale here{1, 1.0, 1, w, h, w, h};
            if (!here.usable()) continue;                      // (a picture narrower than 520 units and shorter than 520 keeps the original's page)
            const int fails_before = g_failures;
            for (size_t rows = 1; rows <= 4; ++rows) {
                for (const int32_t ants : {1, 2}) {
                    for (const int32_t state : {0, 2}) {
                        Case c;
                        c.w = w;
                        c.h = h;
                        c.rows = rows;
                        c.ants = {{ants, ants, ants, ants}};
                        c.report = state > 0;
                        c.play = state == 2;
                        c.is_187 = rows == 3;
                        check_podium(art, c);
                    }
                }
            }
            check_other_pages(art, w, h, std::to_string(w) + " x " + std::to_string(h));
            ++cases;
            bad += g_failures > fails_before ? 1 : 0;
        }
    }
    check(cases > 150 && bad == 0, "every page of " + std::to_string(cases) + " sizes holds (" + std::to_string(bad) + " do not)");
}

void test_pieces(const hud_art::Art& art) {
    group("pieces", "the shipped art has every piece of the page, at the sizes that the layout counts on");
    check(window_art::has_results_page(art), "the shipped set has the new results page");
    for (const char* name : sl::kSprites) check(art.find_sprite(name) >= 0, std::string("the art has ") + name);
    const auto size = [&](const char* name) { const hud_art::Sprite* s = art.sprite_at(art.find_sprite(name)); return s != nullptr ? std::make_pair(s->w, s->h) : std::make_pair(0, 0); };
    for (const char* state : {"up", "hover", "down"}) {
        const std::string st = state;
        check(size(("sc_leave_" + st).c_str()) == std::make_pair(sl::kLeaveW, sl::kLeaveH), "Leave Game is its face of 99 x 22 and its cast shadow, " + st);
        check(size(("sc_watch_" + st).c_str()) == std::make_pair(sl::kWatchW, sl::kWatchH), "Watch is its face of 58 x 20 and its cast shadow, " + st);
        check(size(("sc_btn_m_" + st).c_str()) == std::make_pair(sl::kBigRun, sl::kBigH) && size(("sc_btn_l_" + st).c_str()) == std::make_pair(sl::kBigEnd, sl::kBigH) && size(("sc_btn_r_" + st).c_str()) == std::make_pair(sl::kBigEnd, sl::kBigH),
              "the big button is its two ends and a run, " + st);
        check(size(("sc_tab_m_" + st).c_str()) == std::make_pair(sl::kBigRun, sl::kTabH) && size(("sc_tab_l_" + st).c_str()) == std::make_pair(sl::kBigEnd, sl::kTabH) && size(("sc_tab_r_" + st).c_str()) == std::make_pair(sl::kBigEnd, sl::kTabH),
              "a tab is its two ends and a run, " + st);
    }
    check(size("sc_well_t") == std::make_pair(sl::kWellRun, sl::kWellCorner) && size("sc_well_l") == std::make_pair(sl::kWellCorner, sl::kWellRunY) && size("sc_well_br") == std::make_pair(sl::kWellCorner, sl::kWellCorner), "a well is its corners and the runs of its edges");
    // each lettering sprite holds its line: it shows something, and its baseline and alignment point are inside it
    for (const char* name : {"sc_victory_26", "sc_defeat_22", "sc_gameover_36", "sc_matchstats_15", "sc_others", "sc_g_damage", "sc_s_loot", "sc_k_winner", "sc_k_play", "sc_k_mvp", "sc_k_stand", "sc_leave_word_up", "sc_tw_stats_lit_up"}) {
        const hud_art::Sprite* s = art.sprite_at(art.find_sprite(name));
        check(s != nullptr && s->x >= 0 && s->x <= s->w && s->y > 0 && s->y < s->h && s->w > s->h / 2, std::string(name) + ": its alignment point and baseline are in the sprite");
    }
    check(size("sc_victory_26").first > size("sc_victory_22").first && size("sc_gameover_36").first > size("sc_gameover_26").first, "each title has a big size and a smaller one");
    // a set that lacks a piece, or has one of another size, is not the new page
    hud_art::Art cut;
    std::ifstream f(art_folder() + "/hud_art.txt", std::ios::binary);
    std::string manifest((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const auto without = [&](const char* prefix) {
        std::string out;
        std::istringstream in(manifest);
        std::string line;
        while (std::getline(in, line)) {
            if (line.rfind(prefix, 0) == 0) continue;
            out += line + "\n";
        }
        return out;
    };
    check(cut.parse(without("sprite sc_victory_26 ")) && !window_art::has_results_page(cut), "a set without the title is not the new page");
    hud_art::Art no_tab;
    check(no_tab.parse(without("sprite sc_tab_m_hover ")) && !window_art::has_results_page(no_tab), "a set without a tab's run is not the new page");
    const auto resized = [&](const char* prefix, int32_t dw) {
        std::string out;
        std::istringstream in(manifest);
        std::string line;
        while (std::getline(in, line)) {
            if (line.rfind(prefix, 0) == 0) {
                std::istringstream words(line);
                std::string a, name, w, h, rest;
                words >> a >> name >> w >> h;
                std::getline(words, rest);
                line = a + " " + name + " " + std::to_string(std::stoi(w) + dw) + " " + h + rest;
            }
            out += line + "\n";
        }
        return out;
    };
    hud_art::Art other;
    check(other.parse(resized("sprite sc_well_t ", -1)) && !window_art::has_results_page(other), "a set whose well has an edge of another length is not the new page");
    hud_art::Art other_tab;
    check(other_tab.parse(resized("sprite sc_tab_m_down ", 1)) && !window_art::has_results_page(other_tab), "a set whose tab has a run of another length is not the new page");
    hud_art::Art other_watch;
    check(other_watch.parse(resized("sprite sc_watch_down ", 1)) && !window_art::has_results_page(other_watch), "a set whose Watch is another size is not the new page");
    hud_art::Art none;
    check(!window_art::has_results_page(none), "an art that is not loaded has no page");
}

}  // namespace

void test_scorecard_look(const assets::AssetArchive& arc, const hud_art::Art& art);

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;                                                  // SDL2main renames main to SDL_main(int, char**) on Windows: the signature must be this one
    ensure_sdl();
    assets::AssetArchive arc;
    hud_art::Art art;
    if (!arc.load_chd(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd") || !load_art(art)) {
        std::fprintf(stderr, "cannot open ants.chd or the HUD art\n");
        return 2;
    }
    test_scale();
    test_podium(art);
    test_waiting_and_stats(art);
    test_sweep(art);
    test_pieces(art);
    test_scorecard_look(arc, art);
    std::printf("\nresults screen, new look: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
