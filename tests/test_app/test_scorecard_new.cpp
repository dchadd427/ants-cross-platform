// The results screen in the new look, the model's part (include/ants_app/scorecard_layout.hpp, window_art::has_results_page; docs/VIEW_AND_HUD.md, "Results Screen"): the unit of the page, the layout at
// every size of the list and at a sweep of others, and the art's pieces. The cases that draw are in test_scorecard_new_look.cpp.
//   [scale]   a unit is a whole number of device pixels at 640 x 480, 720p, 1080p, 1440p, an ultrawide and a phone held upright and sideways, with the numbers of the design
//   [layout]  at every one of those sizes, for one to four rows, the 187 page and its headline: nothing is cut, nothing overlaps, every row has its place, and the table and the cards are chosen by the width
//   [sweep]   the same rules at a sweep of sizes (every width and height from a phone to an ultrawide)
//   [pieces]  the shipped art has every piece of the page, at the sizes that the layout counts on; a set that lacks one or has one of another size gives the original's page
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
    int32_t units_w, units_h;      // 0: no number to compare (the shape is the canvas's)
};

const Screen kScreens[] = {
    {"640 x 480", 640, 480, 640, 480, 1, 640, 480},
    {"1280 x 720", 960, 540, 1280, 720, 2, 640, 360},
    {"1920 x 1080", 960, 540, 1920, 1080, 3, 640, 360},
    {"2560 x 1440", 960, 540, 2560, 1440, 4, 640, 360},
    {"3440 x 1440 (21:9)", 1260, 540, 3440, 1440, 4, 840, 360},
    {"phone held upright (1170 x 2532)", 390, 844, 1170, 2532, 3, 390, 844},
    {"phone held sideways (2532 x 1170)", 1200, 540, 2532, 1170, 3, 844, 379},
    {"640 x 480 at 150% (a window of 960 x 720)", 640, 480, 960, 720, 2, 480, 360},       // (the picture that makes the cards today: a 4:3 canvas under 520 units)
};

double output_of(const Screen& s) { return CanvasLayout{s.canvas_w, s.canvas_h}.fit(s.window_w, s.window_h).scale; }

sl::Input input_of(const sl::Scale& scale, size_t rows, int32_t columns, int32_t headline_lines) {
    sl::Input in;
    in.w = scale.w;
    in.h = scale.h;
    in.rows = rows;
    in.columns = columns;
    in.headline_lines = headline_lines;
    return in;
}

bool inside(const LayoutRect& r, int32_t w, int32_t h) { return r.x >= 0 && r.y >= 0 && r.right() <= w && r.bottom() <= h && r.w > 0 && r.h > 0; }

bool overlap(const LayoutRect& a, const LayoutRect& b) { return a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom(); }

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

/// The rectangle of the picture of a well round its ground: 4 more left of and above, 6 more right of and below
LayoutRect well_picture(const LayoutRect& g) { return LayoutRect{g.x - 4, g.y - 4, g.w + 10, g.h + 10}; }

const char* const kScoreSprites[4] = {"sc_score_s", "sc_score_28", "sc_score_33", "sc_score_38"};
const char* const kHeads[4] = {"sc_h_score", "sc_h_friendly", "sc_h_enemy", "sc_h_new"};
const char* const kSubs[4] = {"", "sc_s_lost", "sc_s_killed", "sc_s_hatched"};
const char* const kHeads187[3] = {"sc_h_kills", "sc_h_antslost", "sc_h_antsleft"};

/// Every rule of a layout: what is on the page is on the page, in its order and apart
void check_layout(const hud_art::Art& art, const sl::Input& in, const std::string& at) {
    const sl::Layout l = sl::compute(in);
    const size_t rows = std::min<size_t>(in.rows, sl::kMaxRows);
    check(l.ok && l.row_count == rows, at + ": every row has its place (" + std::to_string(l.row_count) + " of " + std::to_string(rows) + ")");
    if (!l.ok) return;
    const int32_t nc = in.columns >= 4 ? 4 : 3;
    // the page's pieces: the title, "Your Score", the headline's lines, the headings, the labels, the wells with their bezels, Leave Game with its cast shadow
    std::vector<std::pair<std::string, LayoutRect>> parts;
    parts.push_back({"the title", centered_letters(art, "sc_title", l.title_cx, l.title_base)});
    const char* score = kScoreSprites[static_cast<size_t>(l.score)];
    parts.push_back({"Your Score", l.score_centered ? centered_letters(art, score, l.score_x, l.score_base) : letters(art, score, l.score_x, l.score_base)});
    for (int32_t i = 0; i < in.headline_lines; ++i) {
        parts.push_back({"headline line " + std::to_string(i + 1), LayoutRect{l.headline_cx - l.headline_w / 2, l.headline_base + i * l.headline_pitch - 12, l.headline_w, 16}});
    }
    if (l.headings) {
        for (int32_t c = 0; c < nc; ++c) {
            parts.push_back({"heading " + std::to_string(c), letters(art, nc == 3 ? kHeads187[c] : kHeads[c], l.column_right[static_cast<size_t>(c)], l.heading_base)});
            if (nc == 4 && kSubs[c][0] != '\0') parts.push_back({"sub heading " + std::to_string(c), letters(art, kSubs[c], l.column_right[static_cast<size_t>(c)], l.sub_base)});
        }
    }
    parts.push_back({"Winner!", letters(art, "sc_winner", l.label_x, l.winner_base)});
    parts.push_back({"the winner's well", well_picture(l.winner_well)});
    if (rows > 1) {
        parts.push_back({"other players...", letters(art, "sc_others", l.label_x, l.others_base)});
        parts.push_back({"the others' well", well_picture(l.others_well)});
    }
    parts.push_back({"Leave Game", LayoutRect{l.leave.x, l.leave.y, l.leave.w + 3, l.leave.h + 4}});
    for (const auto& p : parts) check(inside(p.second, l.w, l.h), at + ": " + p.first + " is on the page " + str(p.second));
    for (size_t i = 0; i < parts.size(); ++i) {
        for (size_t j = i + 1; j < parts.size(); ++j) {
            const bool letters_a = parts[i].first.find("well") == std::string::npos && parts[i].first != "Leave Game";
            const bool letters_b = parts[j].first.find("well") == std::string::npos && parts[j].first != "Leave Game";
            check(!overlap(letters_a ? ink_of(parts[i].second) : parts[i].second, letters_b ? ink_of(parts[j].second) : parts[j].second),
                  at + ": " + parts[i].first + " " + str(parts[i].second) + " and " + parts[j].first + " " + str(parts[j].second) + " do not overlap");
        }
    }
    // the rows stand in their wells, one after the other, with what they hold apart
    for (size_t i = 0; i < rows; ++i) {
        const sl::RowSlots& r = l.row[i];
        const LayoutRect& well = i == 0 ? l.winner_well : l.others_well;
        check(r.top >= well.y && r.top + r.height <= well.bottom() && r.height > 0, at + ": row " + std::to_string(i) + " is in its well");
        if (i > 0) check(r.top >= l.row[i - 1].top + l.row[i - 1].height || i == 1, at + ": row " + std::to_string(i) + " is under the one before it");
        check(r.name_w > 0 && r.name_x >= well.x && r.number_w > 0, at + ": row " + std::to_string(i) + " has room for its name (" + std::to_string(r.name_w) + ") and its numbers");
        if (l.narrow) {
            check(r.number_base > r.name_base && r.label_base > r.number_base && r.label_base + 3 <= r.top + r.height, at + ": a card holds the name, the numbers and the labels under them, in that order");
            for (int32_t c = 0; c < nc; ++c) {
                const int32_t x = r.number_x[static_cast<size_t>(c)];
                check(x - r.number_w / 2 >= r.name_x - 2 && x + r.number_w / 2 <= well.right(), at + ": a card's number " + std::to_string(c) + " is inside the well");
                if (c > 0) check(x - r.number_w / 2 >= r.number_x[static_cast<size_t>(c - 1)] + r.number_w / 2, at + ": a card's numbers do not touch");
            }
        } else {
            check(r.name_x + r.name_w <= r.number_x[0] - r.number_w, at + ": the name's room ends before the first number's");
            for (int32_t c = 1; c < nc; ++c) check(r.number_x[static_cast<size_t>(c)] - r.number_w >= r.number_x[static_cast<size_t>(c - 1)], at + ": the numbers of a table do not touch");
            check(r.number_x[static_cast<size_t>(nc - 1)] <= well.right() - 4, at + ": the last number ends inside the well");
        }
    }
    // the table and the cards
    check(l.narrow == (in.w < sl::kNarrowBelow && l.leave_big) && l.headings == !l.narrow, at + ": a table has the headings and the cards do not");
    if (in.w >= sl::kNarrowBelow) check(!l.narrow && !l.leave_big, at + ": " + std::to_string(in.w) + " units wide is a table");
    if (l.narrow) check(l.leave.y > l.others_well.bottom() + 6 || (rows == 1 && l.leave.y > l.winner_well.bottom() + 6), at + ": the big button is under the wells");
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
        check(monotonic, at + "every unit edge falls on a pixel of its own (no unit has no width)");
        const LayoutRect r = sc.rect(10, 20, 30, 40);
        check(sc.rect(10, 20, 15, 40).right() == sc.rect(25, 20, 15, 40).x, at + "neighbors share an edge (" + str(r) + ")");
    }
    const sl::Scale odd = sl::scale_for(1000, 540, 4.0 / 3.0);              // (a canvas that is not a whole number of units: the page still ends at the canvas's last pixel)
    check(odd.k == 2 && odd.x(odd.w) == 1000 && odd.y(odd.h) == 540 && odd.rect(0, 0, odd.w, odd.h) == (LayoutRect{0, 0, 1000, 540}), "a canvas of 1000 x 540 is not whole units wide: the last unit edge is the canvas's");
    const sl::Scale tiny = sl::scale_for(640, 480, 0.5);
    check(!tiny.usable() && tiny.k == 1, "a window of 320 x 240 holds 240 units: it keeps the original's page");
    check(sl::scale_for(960, 540, 0.0).k == 1 && sl::scale_for(960, 540, -3.0).k == 1, "an output that is not a number is a pixel to a pixel");
    const sl::Scale four = sl::scale_for(960, 540, 2560.0 / 960.0);
    check(four.k == 4 && four.sheet == 3, "a unit of 4 draws the 3x sheet");
}

void test_layout(const hud_art::Art& art) {
    group("layout", "at every size of the list the page holds every row, with nothing cut and nothing over anything");
    for (const Screen& s : kScreens) {
        const sl::Scale sc = sl::scale_for(s.canvas_w, s.canvas_h, output_of(s));
        for (size_t rows = 1; rows <= 4; ++rows) {
            for (const int32_t columns : {4, 3}) {
                for (const int32_t lines : {0, 1, 2}) {
                    if (columns == 4 && lines > 0) continue;           // (only the 187 page has a headline)
                    check_layout(art, input_of(sc, rows, columns, lines), std::string(s.name) + ", " + std::to_string(rows) + " rows, " + std::to_string(columns) + " numbers, " + std::to_string(lines) + " headline lines");
                }
            }
        }
    }
    // what each size gets
    const sl::Layout wide = sl::compute(input_of(sl::scale_for(960, 540, 2.0), 3, 4, 0));
    check(!wide.narrow && wide.headings && !wide.leave_big && wide.leave.w == 99 && wide.leave.h == 22, "720p, three rows: a table with the headings and the corner's button (99 x 22)");
    check(wide.row[0].number_x[3] > wide.row[0].number_x[2] && wide.row[0].number_x[2] > wide.row[0].number_x[1], "...the number columns run left to right under their headings");
    const sl::Layout phone = sl::compute(input_of(sl::scale_for(390, 844, 3.0), 3, 4, 0));
    check(phone.narrow && !phone.headings && phone.leave_big && phone.leave.w > 300, "a phone held upright, three rows: a card for each player and a big button");
    check(phone.leave.y + phone.leave.h + 4 + 7 <= phone.h, "...the big button is above the bottom edge, with room for its shadow");
    check(phone.score != sl::ScoreSize::Small, "...and there is room for the big \"Your Score\"");
    const sl::Layout tall = sl::compute(input_of(sl::scale_for(960, 540, 2.0), 1, 4, 0));
    check(tall.score == sl::ScoreSize::Big38 && tall.others_well.w == 0, "720p, one row alone: the biggest \"Your Score\", and no well of others");
    const sl::Layout four = sl::compute(input_of(sl::scale_for(960, 540, 2.0), 4, 4, 0));
    check(four.score == sl::ScoreSize::Small && four.row[0].height == 44, "720p, four teams: \"Your Score\" is the small heading beside the column headings, the rows keep their height");
    const sl::Layout crowded = sl::compute(input_of(sl::scale_for(960, 540, 2.0), 4, 3, 2));
    check(crowded.row[0].height >= 28 && crowded.row[0].height < 44, "720p, four teams and a headline of two lines on the 187 page: the rows are lower, to fit");
    const sl::Layout empty = sl::compute(input_of(sl::scale_for(960, 540, 2.0), 0, 4, 0));
    check(!empty.ok && empty.row_count == 0, "no row, no page of rows");
    const sl::Layout five = sl::compute(input_of(sl::scale_for(960, 540, 2.0), 9, 4, 0));
    check(five.row_count == 4, "more rows than there are teams are four");
    // the 187 page's three columns end where the table's four do: the last one at the right edge of the well
    const sl::Layout t4 = sl::compute(input_of(sl::scale_for(960, 540, 2.0), 3, 4, 0));
    const sl::Layout t3 = sl::compute(input_of(sl::scale_for(960, 540, 2.0), 3, 3, 0));
    check(t3.row[0].number_x[2] == t4.row[0].number_x[3] && t3.row[0].number_x[1] == t4.row[0].number_x[2] && t3.column_right[2] == t4.column_right[3], "the 187 page's three columns are the last three of the table's four");
    // the width decides between the table and the cards, at the width and not by a hair
    // the 4:3 picture in a window of 960 x 720 is 480 x 360 units: three rows are cards (the lowest card that fits), four teams do not fit them and are a compact table
    const sl::Scale small = sl::scale_for(640, 480, 1.5);
    const sl::Layout cards3 = sl::compute(input_of(small, 3, 4, 0));
    const sl::Layout table4 = sl::compute(input_of(small, 4, 4, 0));
    check(small.w == 480 && small.h == 360 && cards3.narrow && cards3.leave_big && cards3.row[0].height == 48, "640 x 480 at 1.5: 480 x 360 units, three rows are cards of 48 units and the big button");
    check(!table4.narrow && table4.headings && !table4.leave_big && table4.row[0].name_w > 0 && table4.row[0].number_x[3] <= table4.winner_well.right() - 4, "...four teams are a compact table, with room for the names");
    check(sl::compute(input_of(small, 2, 3, 1)).narrow, "...and the 187 page with one line of headline and two rows is cards too");
    const sl::Layout at519 = sl::compute(input_of(sl::scale_for(519, 900, 1.0), 3, 4, 0));
    const sl::Layout at520 = sl::compute(input_of(sl::scale_for(520, 900, 1.0), 3, 4, 0));
    check(at519.narrow && !at520.narrow, "519 units wide stack the numbers under the names, 520 are a table");
}

void test_sweep(const hud_art::Art& art) {
    group("sweep", "the same rules at every size from a phone to an ultrawide");
    int64_t cases = 0;
    int64_t bad = 0;
    for (int32_t w = 360; w <= 1100; w += (w < 560 ? 10 : 40)) {
        for (int32_t h = 360; h <= 1000; h += (h < 560 ? 10 : 40)) {
            if (w < 480 && h < 600) continue;                  // (no picture is that narrow and that short: a canvas is 4:3 or wider, a phone upright is 1.7 times as high as wide)
            for (size_t rows = 1; rows <= 4; ++rows) {
                for (const int32_t columns : {4, 3}) {
                    for (const int32_t lines : {0, 1, 2}) {
                        if (columns == 4 && lines > 0) continue;                       // (only the 187 page has a headline)
                        const int fails_before = g_failures;
                        check_layout(art, input_of(sl::Scale{1, 1.0, 1, w, h, w, h}, rows, columns, lines),
                                     std::to_string(w) + " x " + std::to_string(h) + ", " + std::to_string(rows) + " rows, " + std::to_string(columns) + " numbers, " + std::to_string(lines) + " headline lines");
                        ++cases;
                        bad += g_failures > fails_before ? 1 : 0;
                    }
                }
            }
        }
    }
    check(cases > 3000 && bad == 0, "every layout of " + std::to_string(cases) + " holds (" + std::to_string(bad) + " do not)");
}

void test_pieces(const hud_art::Art& art) {
    group("pieces", "the shipped art has every piece of the page, at the sizes that the layout counts on");
    check(window_art::has_results_page(art), "the shipped set has the new results page");
    for (const char* name : sl::kSprites) check(art.find_sprite(name) >= 0, std::string("the art has ") + name);
    const auto size = [&](const char* name) { const hud_art::Sprite* s = art.sprite_at(art.find_sprite(name)); return s != nullptr ? std::make_pair(s->w, s->h) : std::make_pair(0, 0); };
    check(size("sc_leave_up") == std::make_pair(sl::kLeaveW, sl::kLeaveH) && size("sc_leave_hover") == std::make_pair(sl::kLeaveW, sl::kLeaveH) && size("sc_leave_down") == std::make_pair(sl::kLeaveW, sl::kLeaveH), "Leave Game is its face of 99 x 22 and its cast shadow, in every state");
    check(size("sc_btn_m_up").first == sl::kBigRun && size("sc_btn_l_down").first == sl::kBigEnd && size("sc_btn_r_hover").second == sl::kBigH, "the big button is its two ends and a run, in every state");
    check(size("sc_well_t") == std::make_pair(sl::kWellRun, sl::kWellCorner) && size("sc_well_l") == std::make_pair(sl::kWellCorner, sl::kWellRunY) && size("sc_well_br") == std::make_pair(sl::kWellCorner, sl::kWellCorner), "a well is its corners and the runs of its edges");
    // each lettering sprite holds its line: it is wider than it is high, shows something, and its baseline and alignment point are inside it
    for (const char* name : {"sc_title", "sc_score_38", "sc_score_s", "sc_winner", "sc_others", "sc_h_score", "sc_h_new", "sc_s_killed", "sc_h_antsleft"}) {
        const hud_art::Sprite* s = art.sprite_at(art.find_sprite(name));
        check(s != nullptr && s->x > 0 && s->x <= s->w && s->y > 0 && s->y < s->h && s->w > s->h / 2, std::string(name) + ": its alignment point and baseline are in the sprite");
    }
    // the playful "Your Score" has three big sizes and a small one, in order
    const auto w_of = [&](const char* name) { return size(name).first; };
    check(w_of("sc_score_28") < w_of("sc_score_33") && w_of("sc_score_33") < w_of("sc_score_38") && w_of("sc_score_s") < w_of("sc_score_28"), "\"Your Score\" has four sizes, each wider than the one before");
    // a set that lacks a piece, or has one of another size, is not the new page
    hud_art::Art cut;
    std::ifstream f(art_folder() + "/hud_art.txt", std::ios::binary);
    std::string manifest((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::string without;
    {
        std::istringstream in(manifest);
        std::string line;
        while (std::getline(in, line)) {
            if (line.rfind("sprite sc_title ", 0) == 0) continue;
            without += line + "\n";
        }
    }
    check(cut.parse(without) && !window_art::has_results_page(cut), "a set without the title is not the new page");
    std::string wrong;
    {
        std::istringstream in(manifest);
        std::string line;
        while (std::getline(in, line)) {
            if (line.rfind("sprite sc_well_t ", 0) == 0) {                        // (the run of the top edge, one unit short)
                std::istringstream words(line);
                std::string a, name, w, h, rest;
                words >> a >> name >> w >> h;
                std::getline(words, rest);
                line = a + " " + name + " " + std::to_string(std::stoi(w) - 1) + " " + h + rest;
            }
            wrong += line + "\n";
        }
    }
    hud_art::Art other;
    check(other.parse(wrong) && !window_art::has_results_page(other), "a set whose well has an edge of another length is not the new page");
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
    test_layout(art);
    test_sweep(art);
    test_pieces(art);
    test_scorecard_look(arc, art);
    std::printf("\nresults screen, new look: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
