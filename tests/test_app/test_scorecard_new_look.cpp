// The results screen in the new look, the cases that draw (src/ants_app/scorecard_new.cpp; docs/VIEW_AND_HUD.md, "Results Screen"): the choice between the new page and the original's, every row and number
// on the page, the 187 page, the cards of a narrow picture, Leave Game's states and clicks, the pixels of the real renderer at whole-number scales, and the application's picture.
//   [switch]  the new page with the art, the original's without it or when the art lacks a piece; the original's page is what it was after the art was given and taken away, to the byte
//   [rows]    every team's row, name and numbers are drawn; the winner's score is gold; four separate teams; a name that is too long ends in three dots
//   [187]     three headings, three numbers a row and the headline
//   [reflow]  a narrow picture stacks the numbers under the names and has the big button
//   [leave]   the three pictures of Leave Game, its click, and nothing before the rows are there
//   [pixels]  the real renderer: the panel covers the picture, a piece is the sheet's own pixels at 1x, 2x and 3x, and no unit is lost at a unit of 4
//   [app]     the application's picture is the whole canvas (the original's page is centred in a wider one), and the click on the button
#include <memory>

#include "hud_art_fixtures.hpp"

#include "ants_app/scorecard.hpp"
#include "ants_app/scorecard_layout.hpp"
#include "ants_app/window_art.hpp"
#include "ants_sim/match_stats.hpp"

namespace sl = ants::app::scorecard_layout;

namespace {

struct Screen {
    const char* name;
    int32_t canvas_w, canvas_h, window_w, window_h;
};

const Screen kWide720{"720p", 960, 540, 1280, 720};
const Screen kPhoneUp{"phone held upright", 390, 844, 1170, 2532};
const Screen kPhoneSide{"phone held sideways", 1200, 540, 2532, 1170};
const Screen kUltra{"ultrawide", 1260, 540, 3440, 1440};
const Screen kSmall43{"640 x 480 at 150%", 640, 480, 960, 720};           // (the picture that makes the cards today)

/// What the page measures its text with (the renderer that the application gives it): a recording renderer with the game's faces
ArtSpy* g_meter = nullptr;

double output_of(const Screen& s) { return CanvasLayout{s.canvas_w, s.canvas_h}.fit(s.window_w, s.window_h).scale; }

/// The results of the game's own sample (`--scorecard`: four teams, the first two allied): rows "A & B" 1500 / 17 / 26 / 40, 420 / 20 / 4 / 10, 100 / 25 / 1 / 5
sim::MatchResult sample(bool allied = true) {
    sim::MatchResult mr{};
    mr.is_over = true;
    if (allied) mr.ally = {1, 0, sim::ALLIANCE_NONE, sim::ALLIANCE_NONE};
    const int32_t scores[4] = {900, 600, 420, 100};
    const uint32_t lost[4] = {5, 12, 20, 25};
    const uint32_t killed[4] = {18, 8, 4, 1};
    const uint32_t hatched[4] = {25, 15, 10, 5};
    for (size_t p = 0; p < 4; ++p) {
        mr.stats[p].score = scores[p];
        mr.stats[p].friendly_lost = lost[p];
        mr.stats[p].enemy_killed = killed[p];
        mr.stats[p].new_hatched = hatched[p];
    }
    mr.decide_winners();
    return mr;
}

void open(ScorecardModal& card, const hud_art::Art* art, const Screen& s, const sim::MatchResult& result, sim::GameMode mode = sim::GameMode::HighestScore, bool rows = true) {
    card.set_text_source(g_meter);
    card.set_art(art);
    card.set_screen(s.canvas_w, s.canvas_h, output_of(s));
    card.set_local_player_name("Ana");
    card.show(result, 0, mode);
    if (rows) card.update(0.25f);
}

std::unique_ptr<ArtSpy> draw(const assets::AssetArchive& arc, ScorecardModal& card, const Screen& s) {
    auto spy = std::make_unique<ArtSpy>(arc);
    spy->face_ready = true;
    spy->dialog_face = true;
    spy->output = output_of(s);
    card.render(*spy, arc);
    return spy;
}

size_t count(const ArtSpy& spy, const hud_art::Art& art, const char* name) { return spy.of_sprite(art, name).size(); }

/// Is the text well formed UTF-8 (every lead byte has its continuation bytes, no stray continuation byte)
bool is_utf8(const std::string& text) {
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        const size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        if (n == 0 || i + n > text.size()) return false;
        for (size_t k = 1; k < n; ++k) {
            if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80) return false;
        }
        i += n;
    }
    return true;
}

/// The parts of a layout that a frame must not move
struct Frozen {
    scorecard_layout::Layout layout;
    scorecard_layout::Scale scale;
    LayoutRect leave;
    bool operator==(const Frozen& o) const {
        return layout.leave == o.layout.leave && layout.winner_well == o.layout.winner_well && layout.others_well == o.layout.others_well && layout.row[0].top == o.layout.row[0].top &&
               layout.row[1].top == o.layout.row[1].top && layout.headline_base == o.layout.headline_base && layout.score == o.layout.score && layout.narrow == o.layout.narrow && scale.k == o.scale.k &&
               scale.w == o.scale.w && scale.h == o.scale.h && leave == o.leave;
    }
};
Frozen frozen_of(const ScorecardModal& card) { return Frozen{card.new_layout(), card.scale(), card.scale().rect(card.new_layout().leave)}; }

/// The layout that a page of these rows and this headline must have: made for as many lines of headline as were drawn
scorecard_layout::Layout expected_layout(const ScorecardModal& card, size_t rows, int32_t lines) {
    scorecard_layout::Input in;
    in.w = card.scale().w;
    in.h = card.scale().h;
    in.rows = rows;
    in.columns = 3;
    in.headline_lines = lines;
    return scorecard_layout::compute(in);
}

bool has_line(const ArtSpy& spy, const std::string& text) { return spy.face(text) != nullptr; }

bool same_events(const ArtSpy& a, const ArtSpy& b) {
    if (a.events.size() != b.events.size()) return false;
    for (size_t i = 0; i < a.events.size(); ++i) {
        const Spy::Ev &x = a.events[i], &y = b.events[i];
        if (x.kind != y.kind || x.name != y.name || x.x != y.x || x.y != y.y || x.w != y.w || x.h != y.h || x.sx != y.sx || x.sy != y.sy || x.sw != y.sw || x.sh != y.sh) return false;
    }
    return true;
}

LayoutRect canvas_rect_of(const ScorecardModal& card, const LayoutRect& units) { return card.scale().rect(units); }

void test_switch(const assets::AssetArchive& arc, const hud_art::Art& art) {
    group("switch", "the new page with the art, the original's without it");
    ScorecardModal fresh;
    open(fresh, nullptr, kWide720, sample());
    check(!fresh.new_look(), "no art: the original's page");
    const auto classic = draw(arc, fresh, kWide720);
    check(!classic->events.empty() && classic->copies.empty(), "...it draws the original's pieces and not one of the art's");

    ScorecardModal dressed;
    open(dressed, &art, kWide720, sample());
    check(dressed.new_look(), "the art with every piece: the new page");
    const auto fresh_new = draw(arc, dressed, kWide720);
    check(count(*fresh_new, art, "panel_c") > 0 && count(*fresh_new, art, "sc_title") == 1 && count(*fresh_new, art, "sc_leave_up") == 1 && count(*fresh_new, art, "sc_winner") == 1, "...the panel, the title, Winner! and Leave Game are the art's");
    check(fresh_new->sheets.size() >= 2 && fresh_new->sheets.front() == dressed.scale().sheet && fresh_new->sheets.back() == 0, "...the sheet of the unit is chosen for it, and the choice is given back (" + std::to_string(fresh_new->sheets.size()) + " calls)");
    check(!has_line(*fresh_new, "Score"), "...the headings are the art's, no text");

    // the art taken away: the original's page, the same as if it had never been given
    dressed.set_art(nullptr);
    check(!dressed.new_look(), "the art taken away: the original's page again");
    const auto back = draw(arc, dressed, kWide720);
    check(same_events(*back, *classic) && back->copies.empty(), "...with every call that it made before the art was given");

    // a set without the title, a set that is not loaded, a canvas of fewer than 360 units: the original's page
    hud_art::Art broken;
    ScorecardModal c1;
    open(c1, &broken, kWide720, sample());
    check(!c1.new_look(), "an art that is not loaded: the original's page");
    ScorecardModal c2;
    open(c2, &art, kWide720, sample());
    c2.set_screen(640, 480, 0.5);
    check(!c2.new_look(), "a window that holds 240 units of height: the original's page");
    c2.set_screen(640, 480, 1.0);
    check(c2.new_look() && c2.scale().k == 1, "...and at its own size, the new page at one device pixel to the unit");
    ScorecardModal c3;
    c3.set_art(&art);
    check(!c3.new_look(), "before the application says what the canvas is: the original's page");
}

void test_rows(const assets::AssetArchive& arc, const hud_art::Art& art) {
    group("rows", "every team's row, name and numbers are on the page");
    ScorecardModal card;
    open(card, &art, kWide720, sample());
    const auto spy = draw(arc, card, kWide720);
    check(card.rows().size() == 3, "the sample has three rows: an alliance and two single teams");
    for (size_t i = 0; i < card.rows().size(); ++i) {
        const ScorecardModal::Row& row = card.rows()[i];
        check(has_line(*spy, row.name), "row " + std::to_string(i) + ": the name \"" + row.name + "\"");
        for (size_t c = 0; c < 4; ++c) check(has_line(*spy, row.numbers[c]), "row " + std::to_string(i) + ": the number " + row.numbers[c]);
    }
    check(card.rows()[0].name.find(" & ") != std::string::npos, "the first row is the alliance");
    const ArtSpy::Face* win = spy->face(card.rows()[0].numbers[0]);
    const ArtSpy::Face* other = spy->face(card.rows()[1].numbers[0]);
    check(win != nullptr && other != nullptr && win->color.r == 246 && win->color.g == 208 && other->color.r == 244 && other->color.b == 226, "the winner's score is gold, the others' numbers cream");
    const sl::Layout& l = card.new_layout();
    check(count(*spy, art, "sc_well_tl") == 2 && count(*spy, art, "sc_others") == 1, "two wells, the winner's and the others', and the label of the others");
    check(count(*spy, art, "sc_h_score") == 1 && count(*spy, art, "sc_h_friendly") == 1 && count(*spy, art, "sc_h_enemy") == 1 && count(*spy, art, "sc_h_new") == 1 && count(*spy, art, "sc_s_hatched") == 1, "a table: the four headings");
    int32_t ants = 0;
    for (const Spy::Ev& e : spy->events) ants += e.kind == Spy::Kind::Region ? 1 : 0;
    check(ants > 0, "the ants of the rows are the game's own pictures (" + std::to_string(ants) + " parts)");
    check(l.row_count == 3 && spy->face(card.rows()[2].name)->baseline == card.scale().y(l.row[2].name_base), "the name stands on the row's baseline");

    // four separate teams
    ScorecardModal four;
    open(four, &art, kWide720, sample(false));
    const auto spy4 = draw(arc, four, kWide720);
    check(four.rows().size() == 4, "four teams without an alliance have four rows");
    for (const ScorecardModal::Row& row : four.rows()) {
        check(has_line(*spy4, row.name) && has_line(*spy4, row.numbers[0]) && has_line(*spy4, row.numbers[3]), "four teams: " + row.name + " and its numbers are drawn");
    }
    check(four.new_layout().score == sl::ScoreSize::Small && count(*spy4, art, "sc_score_s") == 1, "four teams at 720p: the small \"Your Score\"");

    // a name that is too long ends in three dots, and the number beside it is whole
    ScorecardModal longname;
    longname.set_player_names({"Bartholomew the Third of Antwerp, Duke", "Cy", "Dot", "Eve"});
    open(longname, &art, kWide720, sample(false));
    const auto spyl = draw(arc, longname, kWide720);
    bool dotted = false;
    for (const ArtSpy::Face& f : spyl->face_lines) dotted = dotted || (f.text.size() > 3 && f.text.compare(f.text.size() - 3, 3, "...") == 0 && f.text.find("Bartholomew") == 0);
    check(dotted, "a name that does not fit ends in three dots");
    for (const ArtSpy::Face& f : spyl->face_lines) {
        if (f.text.find("Bartholomew") != 0) continue;
        const double w = spyl->face_text_width(f.text, f.em_tenths);
        const sl::RowSlots& r = longname.new_layout().row[0];
        check(f.x + w <= longname.scale().x(r.name_x + r.name_w), "...and it ends before its room does");
    }
    // a number of ten digits is set smaller and stays in its place
    sim::MatchResult big = sample(false);
    big.stats[0].score = 2000000000;
    ScorecardModal huge;
    open(huge, &art, kWide720, big);
    const auto spyh = draw(arc, huge, kWide720);
    const ArtSpy::Face* hf = spyh->face("2000000000");
    check(hf != nullptr && hf->em_tenths < huge.scale().em_tenths(15.0), "a number of ten digits is set smaller, not cut");
}

void test_187(const assets::AssetArchive& arc, const hud_art::Art& art) {
    group("187", "three headings, three numbers a row, and the headline");
    ScorecardModal card;
    open(card, &art, kWide720, sample(), sim::GameMode::Kills187);
    const Frozen before = frozen_of(card);
    const auto spy = draw(arc, card, kWide720);
    check(frozen_of(card) == before, "drawing the frame moves nothing: the layout was made when the rows were built");
    check(!card.headline().empty() && card.new_layout().headings && count(*spy, art, "sc_h_kills") == 1 && count(*spy, art, "sc_h_antslost") == 1 && count(*spy, art, "sc_h_antsleft") == 1, "the three headings of the 187 page");
    check(count(*spy, art, "sc_h_score") == 0 && count(*spy, art, "sc_h_new") == 0, "...and none of the four of the other page");
    for (const ScorecardModal::Row& row : card.rows()) check(has_line(*spy, row.numbers[0]) && has_line(*spy, row.numbers[1]) && has_line(*spy, row.numbers[2]), "a row of 187 shows its three numbers: " + row.name);
    bool headline = false;
    for (const ArtSpy::Line& line : spy->lines) headline = headline || card.headline().find(line.text) == 0;
    check(headline, "the headline \"" + card.headline() + "\" is in the windows' face");
    check(card.new_layout().headline_base > 0 && card.new_layout().row[0].top > card.new_layout().headline_base, "the headline is above the rows");
    // two lines for a long headline: the room for them is made when the rows are built, and the first frame does not move it
    ScorecardModal narrow;
    narrow.set_player_names({"Bartholomew the Third of Antwerp", "Cornelius the Mighty Builder of Hills", "Dot", "Eve"});
    sim::MatchResult both = sample();
    both.standing = 0;
    both.ants_left = {5, 4, 0, 0};
    open(narrow, &art, kPhoneUp, both, sim::GameMode::Kills187);
    const Frozen narrow_before = frozen_of(narrow);
    const auto spyn = draw(arc, narrow, kPhoneUp);
    check(narrow.new_layout().narrow && spyn->lines.size() >= 2 && narrow.new_layout().headline_base > 0, "a long headline on a phone is two lines");
    check(frozen_of(narrow) == narrow_before, "...and the frame that draws them moves nothing");
    const scorecard_layout::Layout made_for = expected_layout(narrow, narrow.rows().size(), static_cast<int32_t>(spyn->lines.size()));
    check(narrow.new_layout().row[0].top == made_for.row[0].top && narrow.new_layout().winner_well == made_for.winner_well && narrow.new_layout().leave == made_for.leave, "...the page was laid out for as many lines as were drawn (" + std::to_string(spyn->lines.size()) + ")");
    check(count(*spyn, art, "sc_btn_m_up") >= 1, "...and the page is the cards' page");
    // a headline of three lines and more is cut to two: whole characters only, whatever the names are made of (a name a letter longer moves the cut by a byte: eight of them cover every place a cut can fall)
    int whole_runs = 0;
    int cut_runs = 0;
    for (int extra = 0; extra < 8; ++extra) {
        ScorecardModal utf;
        const std::string pad(static_cast<size_t>(extra), 'x');
        utf.set_player_names({"\xC3\x9C" "n\xC3\xAF" "c\xC3\xB6" "d\xC3\xA9 W\xC3\xA4" "rri\xC3\xB6" "r Qu\xC3\xA9\xC3\xA9" "n of the \xC3\x81" "ntz", pad + "\xC3\x89" "l\xC3\xA9" "onore \xC3\x89" "tienne \xC3\x86" "gir \xC3\x98" "rsted \xC3\x85" "sa \xC3\x9C" "nal \xC3\x87" "elik \xC3\x8F" "\xC3\xB1" "igo \xC3\x96" "zt\xC3\xBC" "rk Zo\xC3\xAB Bront\xC3\xAB", "Dot", "Eve"});
        sim::MatchResult many = sample();
        many.standing = 0;
        many.ants_left = {5, 4, 0, 0};
        open(utf, &art, kPhoneUp, many, sim::GameMode::Kills187);
        const auto spyu = draw(arc, utf, kPhoneUp);
        bool all_whole = !spyu->lines.empty();
        for (const ArtSpy::Line& line : spyu->lines) all_whole = all_whole && is_utf8(line.text);
        const bool dotted = spyu->lines.size() == 2 && spyu->lines[1].text.size() > 3 && spyu->lines[1].text.compare(spyu->lines[1].text.size() - 3, 3, "...") == 0;
        whole_runs += all_whole && dotted ? 1 : 0;
        cut_runs += dotted ? 1 : 0;
    }
    check(cut_runs == 8, "a headline of long names with accents is cut to two lines, the end of the second with three dots (" + std::to_string(cut_runs) + " of 8)");
    check(whole_runs == 8, "...at a whole character each time: every line that is drawn is well formed UTF-8 (" + std::to_string(whole_runs) + " of 8)");
}

void test_reflow(const assets::AssetArchive& arc, const hud_art::Art& art) {
    group("reflow", "a narrow picture stacks the numbers under the names");
    ScorecardModal card;
    open(card, &art, kPhoneUp, sample());
    const auto spy = draw(arc, card, kPhoneUp);
    check(card.new_look() && card.new_layout().narrow && card.scale().k == 3, "a phone held upright: the cards, at 3 device pixels to the unit");
    check(count(*spy, art, "sc_h_score") == 0 && count(*spy, art, "sc_score_38") + count(*spy, art, "sc_score_33") + count(*spy, art, "sc_score_28") == 1, "...no column headings, the big \"Your Score\"");
    check(count(*spy, art, "sc_leave_up") == 0 && count(*spy, art, "sc_btn_l_up") == 1 && count(*spy, art, "sc_btn_r_up") == 1 && count(*spy, art, "sc_btn_m_up") >= 1 && count(*spy, art, "sc_leave_word_up") == 1, "...Leave Game is the big button: its ends, its run and its word");
    check(has_line(*spy, "Score") && has_line(*spy, "Lost") && has_line(*spy, "Killed") && has_line(*spy, "Hatched"), "...each number has its little label under it");
    for (const ScorecardModal::Row& row : card.rows()) {
        const ArtSpy::Face* name = spy->face(row.name);
        const ArtSpy::Face* number = spy->face(row.numbers[0]);
        check(name != nullptr && number != nullptr && number->baseline > name->baseline, "a card has the numbers under the name: " + row.name);
    }
    const sl::Layout& l = card.new_layout();
    const LayoutRect big = canvas_rect_of(card, l.leave);
    check(big.w > card.scale().canvas_w * 8 / 10, "the big button is most of the picture wide");

    // the picture that makes the cards today: the 4:3 canvas in a window of 960 x 720 (480 x 360 units)
    ScorecardModal small;
    open(small, &art, kSmall43, sample());
    const auto spys = draw(arc, small, kSmall43);
    check(small.new_look() && small.scale().k == 2 && small.scale().w == 480 && small.scale().h == 360 && small.new_layout().narrow, "640 x 480 at 150%: 480 x 360 units, the cards");
    check(count(*spys, art, "sc_btn_m_up") >= 1 && count(*spys, art, "sc_h_score") == 0 && has_line(*spys, "Killed"), "...with the big button, the labels under the numbers and no column headings");
    for (const ScorecardModal::Row& row : small.rows()) {
        const ArtSpy::Face* name = spys->face(row.name);
        const ArtSpy::Face* number = spys->face(row.numbers[0]);
        check(name != nullptr && number != nullptr && number->baseline > name->baseline, "640 x 480 at 150%: the numbers are under the name: " + row.name);
    }
    ScorecardModal small4;
    open(small4, &art, kSmall43, sample(false));
    const auto spys4 = draw(arc, small4, kSmall43);
    check(small4.rows().size() == 4 && !small4.new_layout().narrow && count(*spys4, art, "sc_h_score") == 1 && count(*spys4, art, "sc_leave_up") == 1, "640 x 480 at 150%, four teams: a compact table, with the corner's button");
    for (const ScorecardModal::Row& row : small4.rows()) check(spys4->face(row.numbers[3]) != nullptr && spys4->face(row.name) != nullptr, "...every row is there: " + row.name);

    ScorecardModal side;
    open(side, &art, kPhoneSide, sample());
    check(side.new_look() && !side.new_layout().narrow && side.new_layout().headings, "a phone held sideways: the table");
    ScorecardModal ultra;
    open(ultra, &art, kUltra, sample());
    check(ultra.new_look() && ultra.scale().k == 4 && ultra.new_layout().content.w <= sl::kMaxContent, "an ultrawide: the column is no wider than 760 units, centered");
    check(ultra.new_layout().content.x * 2 + ultra.new_layout().content.w == ultra.scale().w || ultra.new_layout().content.x * 2 + ultra.new_layout().content.w == ultra.scale().w - 1, "...in the middle");
}

void test_leave(const assets::AssetArchive& arc, const hud_art::Art& art) {
    group("leave", "Leave Game in its three states, and nothing before the rows");
    ScorecardModal waiting;
    open(waiting, &art, kWide720, sample(), sim::GameMode::HighestScore, false);
    int quits = 0;
    waiting.set_on_quit([&]() { ++quits; });
    const auto wait = draw(arc, waiting, kWide720);
    check(waiting.is_waiting() && wait->face("Waiting for scores...") != nullptr && count(*wait, art, "sc_leave_up") == 0, "the waiting screen has its sentence and no button");
    check(count(*wait, art, "sc_well_tl") == 2 && count(*wait, art, "sc_title") == 1, "...the page's wells and title are there");
    const LayoutRect face = canvas_rect_of(waiting, waiting.new_layout().leave);
    waiting.handle_mouse_down(face.x + 5, face.y + 5);
    waiting.handle_mouse_up(face.x + 5, face.y + 5);
    check(quits == 0 && !waiting.is_quit_pressed(), "...a click where the button will be does nothing before the rows are there");

    ScorecardModal card;
    open(card, &art, kWide720, sample());
    card.set_on_quit([&]() { ++quits; });
    int clicks = 0;
    card.set_on_play_sfx([&](uint32_t) { ++clicks; });
    const LayoutRect b = canvas_rect_of(card, card.new_layout().leave);
    check(b.w > 90 && b.h > 30, "the button's face is " + str(b) + " canvas pixels");
    check(count(*draw(arc, card, kWide720), art, "sc_leave_up") == 1, "at rest: the up picture");
    card.handle_mouse_motion(b.x + b.w / 2, b.y + b.h / 2);
    auto over = draw(arc, card, kWide720);
    check(count(*over, art, "sc_leave_hover") == 1 && count(*over, art, "sc_leave_up") == 0, "pointed at: the hover picture");
    card.handle_mouse_down(b.x + b.w / 2, b.y + b.h / 2);
    auto down = draw(arc, card, kWide720);
    check(count(*down, art, "sc_leave_down") == 1 && clicks == 1 && quits == 0, "pressed: the pressed picture, the click's sound, and nothing leaves yet");
    card.handle_mouse_up(b.x + b.w / 2, b.y + b.h / 2);
    check(quits == 1 && count(*draw(arc, card, kWide720), art, "sc_leave_hover") == 1, "released on it: it leaves once");
    card.handle_mouse_down(b.x + 3, b.y + 3);
    card.handle_mouse_motion(b.x - 20, b.y + 3);
    card.handle_mouse_up(b.x - 20, b.y + 3);
    check(quits == 1, "pressed on it and released away from it: it does not leave");
    card.handle_mouse_down(b.x + b.w, b.y + 3);
    card.handle_mouse_up(b.x + b.w, b.y + 3);
    check(quits == 1, "the pixel right of the face is not the button");
    card.handle_mouse_down(b.x + b.w - 1, b.y + b.h - 1);
    card.handle_mouse_up(b.x + b.w - 1, b.y + b.h - 1);
    check(quits == 2, "the last pixel of the face is");
    card.handle_mouse_down(b.x + 1, b.y + 1);
    card.handle_mouse_up(b.x + 1, b.y + 1);
    check(quits == 3, "the first one too");
    card.leave();
    check(quits == 4, "Enter, C, Q and X leave at any time (the application calls leave())");
    card.hide();
    card.handle_mouse_down(b.x + 5, b.y + 5);
    card.handle_mouse_up(b.x + 5, b.y + 5);
    check(quits == 4, "a closed screen takes no click");

    // the big button of the cards
    ScorecardModal phone;
    open(phone, &art, kPhoneUp, sample());
    int phone_quits = 0;
    phone.set_on_quit([&]() { ++phone_quits; });
    const LayoutRect pb = canvas_rect_of(phone, phone.new_layout().leave);
    phone.handle_mouse_down(pb.x + pb.w / 2, pb.y + pb.h / 2);
    const auto pd = draw(arc, phone, kPhoneUp);
    check(count(*pd, art, "sc_btn_m_down") >= 1 && count(*pd, art, "sc_leave_word_down") == 1, "the big button, pressed: its pressed ends, run and word");
    phone.handle_mouse_up(pb.x + pb.w / 2, pb.y + pb.h / 2);
    check(phone_quits == 1, "...and it leaves");
}

// ---- the real renderer ------------------------------------------------------------------------------------------------------------------------------------------------

struct Rig {
    Rig(const assets::AssetArchive& archive, const hud_art::Art& art_set, int32_t cw, int32_t ch, int32_t ww, int32_t wh) : window_w(ww), window_h(wh) {
        const QuietStdout quiet;
        ensure_sdl();
        win = SDL_CreateWindow("scorecard", 0, 0, ww, wh, SDL_WINDOW_HIDDEN);
        if (win == nullptr || !renderer.init(win, archive)) return;
        renderer.set_canvas_size(cw, ch);
        renderer.set_dialog_font_path(art_set.folder() + "LibreBaskerville-Regular.ttf");
        ok = renderer.preload_art(art_set);
    }
    ~Rig() {
        renderer.shutdown();
        if (win != nullptr) SDL_DestroyWindow(win);
    }
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;
    std::vector<uint8_t> read() {
        std::vector<uint8_t> px(static_cast<size_t>(window_w) * static_cast<size_t>(window_h) * 4u, 0);
        SDL_RenderReadPixels(renderer.get_sdl_renderer(), nullptr, SDL_PIXELFORMAT_RGBA32, px.data(), window_w * 4);
        return px;
    }
    int32_t window_w, window_h;
    SDL_Window* win{nullptr};
    Renderer renderer;
    bool ok{false};
};

/// How many pixels of the picture are the black behind everything (SDL_RenderReadPixels without a rectangle reads the logical viewport, so the picture is the top left of the buffer and the bars are not in it), outside the wells (the ants, which have black in them, stand in the wells) and outside the
/// small words under the headings (which are black ink): the panel leaves none, so none of the picture is missing
int64_t black_behind(const std::vector<uint8_t>& px, int32_t cw, int32_t ch, int32_t ww, int32_t wh, const ScorecardModal& card, const hud_art::Art& art) {
    const CanvasFit fit = CanvasLayout{cw, ch}.fit(ww, wh);
    const double o = fit.scale;
    std::vector<LayoutRect> wells;
    const auto window_of = [&](const LayoutRect& units_rect) {
        const LayoutRect c = card.scale().rect(units_rect);
        wells.push_back(LayoutRect{static_cast<int32_t>(std::floor(c.x * o)), static_cast<int32_t>(std::floor(c.y * o)), static_cast<int32_t>(std::ceil(c.w * o)) + 1,
                                   static_cast<int32_t>(std::ceil(c.h * o)) + 1});
    };
    for (const LayoutRect& g : {card.new_layout().winner_well, card.new_layout().others_well}) window_of(LayoutRect{g.x - 4, g.y - 4, g.w + 10, g.h + 10});
    if (card.new_layout().headings) {
        for (const char* name : {"sc_s_lost", "sc_s_killed", "sc_s_hatched"}) {
            const hud_art::Sprite* sp = art.sprite_at(art.find_sprite(name));
            const size_t c = std::string(name) == "sc_s_lost" ? 1u : std::string(name) == "sc_s_killed" ? 2u : 3u;
            if (sp != nullptr) window_of(LayoutRect{card.new_layout().column_right[c] - sp->x, card.new_layout().sub_base - sp->y, sp->w, sp->h});
        }
    }
    int64_t black = 0;
    for (int32_t y = 0; y < fit.viewport.h; ++y) {
        for (int32_t x = 0; x < fit.viewport.w; ++x) {
            bool in_well = false;
            for (const LayoutRect& w : wells) in_well = in_well || w.contains(x, y);
            const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(ww) + static_cast<size_t>(x)) * 4u;
            if (!in_well && px[i] == 0 && px[i + 1] == 0 && px[i + 2] == 0) ++black;
        }
    }
    return black;
}

/// How many black pixels the cover check may find: none at a whole-number output scale; at another one the test's software rasterizer (the dummy driver) cuts every rectangle to whole pixels on its own and
/// may leave a hairline where two meet (two or three rows of the picture at the most: a GPU draws the shared edge once)
int64_t black_allowed(const Screen& s) {
    const double o = output_of(s);
    return std::fabs(o - std::round(o)) < 1e-6 ? 0 : static_cast<int64_t>(3) * s.window_w;
}

std::vector<uint8_t> frame_of(Rig& rig, const assets::AssetArchive& arc, ScorecardModal& card) {
    rig.renderer.begin_frame();
    card.render(rig.renderer, arc);
    return rig.read();
}

void test_pixels(const assets::AssetArchive& arc, const hud_art::Art& art) {
    group("pixels", "the real renderer: the whole picture is covered, and a piece is the sheet's own pixels at a whole-number scale");
    // the original's page is what it was after the art was given and taken away, to the byte
    {
        Rig rig(arc, art, 960, 540, 960, 540);
        check(rig.ok, "the renderer is up with the art's sheet");
        ScorecardModal never;
        never.set_wide_layout(true);
        never.set_local_player_name("Ana");
        never.show(sample(), 0);
        never.update(0.25f);
        const std::vector<uint8_t> before = frame_of(rig, arc, never);
        ScorecardModal dressed;
        dressed.set_wide_layout(true);
        dressed.set_local_player_name("Ana");
        dressed.set_art(&art);
        dressed.set_screen(960, 540, 1.0);
        dressed.show(sample(), 0);
        dressed.update(0.25f);
        check(dressed.new_look(), "the art is given");
        const std::vector<uint8_t> with = frame_of(rig, arc, dressed);
        dressed.set_art(nullptr);
        const std::vector<uint8_t> after = frame_of(rig, arc, dressed);
        int64_t differ = 0;
        for (size_t i = 0; i < before.size(); ++i) differ += before[i] != with[i] ? 1 : 0;
        check(!before.empty() && before == after, "the original's page, after the art is taken away, is the page that was never dressed: the same bytes");
        check(differ > static_cast<int64_t>(before.size()) / 8, "...and the new page is another picture (" + std::to_string(differ) + " bytes differ)");
    }
    // whole-number scales: canvas and window are the same, a unit is 1, 2 or 3 pixels, the sheet is the unit's, and the Leave button is the sheet's own picture
    const struct { int32_t w, h; } sizes[] = {{640, 480}, {1280, 720}, {1920, 1080}};
    for (const auto& sz : sizes) {
        const std::string at = std::to_string(sz.w) + " x " + std::to_string(sz.h) + ": ";
        Rig rig(arc, art, sz.w, sz.h, sz.w, sz.h);
        ScorecardModal card;
        card.set_art(&art);
        card.set_screen(sz.w, sz.h, 1.0);
        card.set_local_player_name("Ana");
        card.show(sample(), 0);
        card.update(0.25f);
        const std::vector<uint8_t> px = frame_of(rig, arc, card);
        const int32_t k = card.scale().k;
        check(card.new_look() && card.scale().f == static_cast<double>(k) && card.scale().sheet == k, at + "a unit is " + std::to_string(k) + " pixels and the sheet is " + std::to_string(k) + "x");
        const int64_t black = black_behind(px, sz.w, sz.h, sz.w, sz.h, card, art);
        check(black == 0, at + "the panel covers the whole picture: no pixel of the black behind (" + std::to_string(black) + ")");
        DecodedImage sheet;
        const hud_art::Sprite* leave = art.sprite_at(art.find_sprite("sc_leave_up"));
        check(art.sheet_pixels(k, sheet) && leave != nullptr, at + "the sheet is read");
        if (leave == nullptr || sheet.rgba.empty()) continue;
        const LayoutRect at_units = card.new_layout().leave;
        int64_t opaque = 0, exact = 0;
        for (int32_t y = 0; y < leave->h * k; ++y) {
            for (int32_t x = 0; x < leave->w * k; ++x) {
                const size_t s = (static_cast<size_t>(leave->sheet_y * k + y) * static_cast<size_t>(sheet.width) + static_cast<size_t>(leave->sheet_x * k + x)) * 4u;
                if (sheet.rgba[s + 3] != 255) continue;
                const size_t d = (static_cast<size_t>(at_units.y * k + y) * static_cast<size_t>(sz.w) + static_cast<size_t>(at_units.x * k + x)) * 4u;
                ++opaque;
                exact += (px[d] == sheet.rgba[s] && px[d + 1] == sheet.rgba[s + 1] && px[d + 2] == sheet.rgba[s + 2]) ? 1 : 0;
            }
        }
        check(opaque > 1000 && exact == opaque, at + "every opaque pixel of Leave Game is the sheet's own (" + std::to_string(exact) + " of " + std::to_string(opaque) + ")");
    }
    // a unit of 4 (1440p) and a window that is not a multiple of the canvas: the picture is covered, and Leave Game shows
    {
        const Screen s{"1440p", 960, 540, 2560, 1440};
        Rig rig(arc, art, s.canvas_w, s.canvas_h, s.window_w, s.window_h);
        ScorecardModal card;
        open(card, &art, s, sample());
        const std::vector<uint8_t> px = frame_of(rig, arc, card);
        const int64_t black = black_behind(px, s.canvas_w, s.canvas_h, s.window_w, s.window_h, card, art);
        check(card.scale().k == 4 && card.scale().sheet == 3 && black <= black_allowed(s), "1440p: a unit of 4 on the 3x sheet, the panel covers the picture (" + std::to_string(black) + " black pixels)");
        const LayoutRect b = canvas_rect_of(card, card.new_layout().leave);
        const double o = output_of(s);
        const size_t mid = (static_cast<size_t>(std::lround((b.y + b.h / 2) * o)) * static_cast<size_t>(s.window_w) + static_cast<size_t>(std::lround((b.x + b.w / 2) * o))) * 4u;
        check(px[mid + 1] > px[mid + 0] && px[mid + 1] < 140, "...the button's face is green steel where the pointer would be");
    }
    // the new page differs between a 4:3 canvas and a 21:9 one only by what they have room for: both are covered
    for (const Screen& s : {kUltra, kPhoneSide, kPhoneUp, kSmall43}) {
        Rig rig(arc, art, s.canvas_w, s.canvas_h, s.window_w, s.window_h);
        ScorecardModal card;
        open(card, &art, s, sample());
        const std::vector<uint8_t> px = frame_of(rig, arc, card);
        const int64_t black = black_behind(px, s.canvas_w, s.canvas_h, s.window_w, s.window_h, card, art);
        check(card.new_look() && black <= black_allowed(s), std::string(s.name) + ": the panel covers the picture (" + std::to_string(black) + " black pixels)");
    }
}

// ---- the application -------------------------------------------------------------------------------------------------------------------------------------------------

SDL_Window* find_window() {
    for (uint32_t i = 1; i < 256; ++i) {
        if (SDL_Window* w = SDL_GetWindowFromID(i)) return w;
    }
    return nullptr;
}

/// A headless application on SDL's dummy driver, its window the size of its canvas
struct AppRig {
    AppRig(Aspect aspect, bool new_art, double window_scale = 1.0) : scale(window_scale) {
        static int counter = 0;
        settings = std::filesystem::temp_directory_path() / ("ants_scorecard_new_" + std::to_string(static_cast<unsigned long long>(SDL_GetPerformanceCounter())) + "_" + std::to_string(counter++) + ".ini");
        std::error_code ignore;
        std::filesystem::remove(settings, ignore);
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.skip_intro = true;
        cfg.aspect = aspect;
        cfg.aspect_given = true;
        cfg.start_in_map_select = true;
        cfg.lan_port = 0;
        cfg.settings_path = settings.string();
        cfg.screenshot_path = settings.string() + ".png";
        cfg.screenshot_frames = 1000000000;
        cfg.has_window_size = true;
        cfg.window_w = static_cast<int32_t>(std::lround(canvas_width_of(aspect) * window_scale));
        cfg.window_h = static_cast<int32_t>(std::lround(canvas_height_of(aspect) * window_scale));
        cfg.hud_art_given = true;
        cfg.hud_art_new = new_art;
        cfg.hud_art_dir = art_folder();
        QuietStdout quiet;
        ok = app.init(cfg);
        window = find_window();
    }
    ~AppRig() {
        app.shutdown();
        std::error_code ignore;
        std::filesystem::remove(settings, ignore);
    }
    AppRig(const AppRig&) = delete;
    AppRig& operator=(const AppRig&) = delete;
    /// A pointer event at the canvas pixel (x, y): SDL converts the window's pixels of an event to the canvas's (the logical size), so the event is given in the window's, at the middle of that pixel
    void event(Uint32 type, int32_t cx, int32_t cy) {
        const int32_t x = static_cast<int32_t>(std::floor((cx + 0.5) * scale));
        const int32_t y = static_cast<int32_t>(std::floor((cy + 0.5) * scale));
        SDL_Event e{};
        e.type = type;
        if (type == SDL_MOUSEMOTION) {
            e.motion.windowID = SDL_GetWindowID(window);
            e.motion.x = x;
            e.motion.y = y;
        } else {
            e.button.windowID = SDL_GetWindowID(window);
            e.button.button = SDL_BUTTON_LEFT;
            e.button.state = type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
            e.button.clicks = 1;
            e.button.x = x;
            e.button.y = y;
        }
        SDL_PushEvent(&e);
    }
    void deliver() { app.run_frame_with_delta(0.001f); }
    void click(int32_t x, int32_t y) {
        event(SDL_MOUSEMOTION, x, y);
        event(SDL_MOUSEBUTTONDOWN, x, y);
        deliver();
        event(SDL_MOUSEBUTTONUP, x, y);
        deliver();
    }
    void results() {
        app.hud().update(app.sim().get_world_state(), 100);
        app.scorecard().show(sample(), 0);
        app.update_results(0.3f);
        deliver();
    }
    double scale;                                     // device pixels to a canvas pixel
    std::filesystem::path settings;
    Application app;
    SDL_Window* window{nullptr};
    bool ok{false};
};

void test_app() {
    group("app", "the application's picture is the new page's whole canvas, and the click lands on the button");
    for (const Aspect aspect : {Aspect::Ultra21x9, Aspect::Wide16x9, Aspect::Classic4x3}) {
        const CanvasLayout canvas = CanvasLayout::of(aspect);
        const std::string at = std::string(aspect_name(aspect)) + ": ";
        AppRig rig(aspect, true);
        check(rig.ok && rig.app.start_game("Original-Ants/Maps/SMALL.LVL"), at + "a match starts");
        int quits = 0;
        rig.app.scorecard().set_on_quit([&]() { ++quits; });
        rig.results();
        const ScorecardModal& card = rig.app.scorecard();
        check(card.is_open() && !card.is_waiting() && card.new_look() && rig.app.picture() == canvas.rect(), at + "New art: the results are the new page and the picture is the whole canvas " + str(rig.app.picture()));
        const LayoutRect b = card.scale().rect(card.new_layout().leave);
        rig.click(b.x - 30, b.y + b.h / 2);
        check(quits == 0, at + "a click beside the button does nothing");
        rig.click(b.x + b.w / 2, b.y + b.h / 2);
        check(quits == 1, at + "a click on the button leaves");
    }
    // the 4:3 picture in a window of 960 x 720: 480 x 360 units, the cards, and the click on the big button
    {
        AppRig rig(Aspect::Classic4x3, true, 1.5);
        check(rig.ok && rig.app.start_game("Original-Ants/Maps/SMALL.LVL"), "4:3 at 150%: a match starts");
        int quits = 0;
        rig.app.scorecard().set_on_quit([&]() { ++quits; });
        rig.results();
        const ScorecardModal& card = rig.app.scorecard();
        check(card.is_open() && card.new_look() && card.new_layout().narrow && card.scale().w == 480 && card.scale().h == 360 && rig.app.picture() == CanvasLayout::of(Aspect::Classic4x3).rect(),
              "4:3 at 150%: the results are the cards' page, 480 x 360 units, and the picture is the whole canvas " + str(rig.app.picture()));
        const LayoutRect b = card.scale().rect(card.new_layout().leave);
        check(b.w > 400 && b.bottom() <= 480, "...the big button is most of the picture wide and inside it " + str(b));
        rig.click(b.x + b.w / 2, b.y - 12);
        check(quits == 0, "...a click above the button does nothing");
        rig.click(b.x + b.w / 2, b.y + b.h / 2);
        check(quits == 1, "...a click on the big button leaves");
    }
    // Classic art: the original's page, centred in the canvas when the canvas is wider
    {
        AppRig rig(Aspect::Ultra21x9, false);
        check(rig.ok && rig.app.start_game("Original-Ants/Maps/SMALL.LVL"), "Classic 21:9: a match starts");
        rig.results();
        check(rig.app.scorecard().is_open() && !rig.app.scorecard().new_look() && rig.app.picture() == CanvasLayout::of(Aspect::Ultra21x9).page(), "Classic art: the original's page, 960 x 540 in the middle of the canvas");
    }
    // the setting changes the page at once, and back
    {
        AppRig rig(Aspect::Ultra21x9, true);
        check(rig.ok && rig.app.start_game("Original-Ants/Maps/SMALL.LVL"), "the setting: a match starts");
        rig.results();
        check(rig.app.scorecard().new_look() && rig.app.picture() == CanvasLayout::of(Aspect::Ultra21x9).rect(), "the setting: New");
        check(rig.app.game_menu_set(17, 1), "...Classic is chosen");
        rig.deliver();
        check(!rig.app.scorecard().new_look() && rig.app.picture() == CanvasLayout::of(Aspect::Ultra21x9).page(), "...the next frame is the original's page");
        check(rig.app.game_menu_set(17, 0), "...New is chosen");
        rig.deliver();
        check(rig.app.scorecard().new_look() && rig.app.picture() == CanvasLayout::of(Aspect::Ultra21x9).rect(), "...and the next is the new page again");
    }
}

}  // namespace

void test_scorecard_look(const assets::AssetArchive& arc, const hud_art::Art& art) {
    ArtSpy meter(arc);                                // (what the page measures its text with, as the application's renderer is: the game's faces)
    meter.face_ready = true;
    meter.dialog_face = true;
    g_meter = &meter;
    test_switch(arc, art);
    test_rows(arc, art);
    test_187(arc, art);
    test_reflow(arc, art);
    test_leave(arc, art);
    test_pixels(arc, art);
    test_app();
    g_meter = nullptr;
}
