// The results screen in the new look, the cases that draw (src/ants_app/scorecard_new.cpp; docs/VIEW_AND_HUD.md, "The new look of the results screen"): the choice between the new page and the original's, the
// podium's rows and numbers, the counted numbers (honors, the Play of the game, the damage) and the page that holds them, the waiting page, the 187 page, a narrow picture, the buttons and their clicks, the
// pixels of the real renderer at whole-number scales, and the application's picture.
//   [switch]   the new page with the art, the original's without it or when the art lacks a piece; the original's page is what it was after the art was given and taken away, to the byte
//   [podium]   the title, the sentence, the winner's well, every team's row and numbers; the winner's score is gold; four separate teams; a name that is too long ends in three dots
//   [counted]  a match whose numbers were counted: the link "All stats", the damage, the honors, the Play of the game and Watch, the second page; a match whose numbers were not shows the original's and nothing else
//   [waiting]  "Game Over" and its line, and Leave Game from the first moment
//   [187]      the headline in the sentence, two tiles, Last stand, six columns of the second page
//   [reflow]   a narrow picture stacks the page in one column and has the big button
//   [buttons]  the three pictures of Leave Game, the link to the other page and Watch, their clicks
//   [room]     the match of a lobby room: "Back to the room" and "Back to Ants & Uncles" in place of Leave Game, on every page, until one is pressed
//   [pixels]   the real renderer: the panel covers the picture, a piece is the sheet's own pixels at 1x, 2x and 3x, and no unit is lost at a unit of 4
//   [app]      the application's picture is the whole canvas (the original's page is centred in a wider one), and the click on the button
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
const Screen kSmall43{"640 x 480 at 150%", 640, 480, 960, 720};

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

/// A match that was counted: 12:40 long, the damage of each team, the honors and a bomb at 7:41 that took out six ants
MatchReport counted(bool with_play = true) {
    MatchReport r;
    r.ok = true;
    r.turns = 15200;
    r.roster = 0x0F;
    r.winners = 0x03;
    r.damage_done = {1240, 600, 420, 90};
    r.damage_taken = {200, 300, 500, 1350};
    r.food_trips = {30, 20, 10, 5};
    r.food_points = {900, 600, 400, 100};
    r.bombs_set = {2, 1, 0, 0};
    r.fires_lit = {1, 0, 0, 0};
    r.raids = {4, 0, 1, 0};
    r.loot = {150, 0, 20, 0};
    r.raids_home = {3, 0, 1, 0};                                  // (seat 0 began four raids and got three of them home)
    r.loot_home = {100, 0, 20, 0};
    r.mvp = MatchReport::Honor{true, 0, 620, 0};
    r.first_blood = MatchReport::Honor{true, 2, 0, 840};
    r.top_thief = MatchReport::Honor{true, 0, 3, 0};
    r.last_stand = MatchReport::Honor{true, 3, 0, 2600};
    if (with_play) r.play = MatchReport::Play{true, 1, MatchReport::PlayKind::Bomb, 6, 9220, 10, 20};
    return r;
}

void open(ScorecardModal& card, const hud_art::Art* art, const Screen& s, const sim::MatchResult& result, sim::GameMode mode = sim::GameMode::HighestScore, bool rows = true, const MatchReport* report = nullptr,
          uint8_t local = 0, bool room_choice = false) {
    card.set_art(art);
    card.set_room_choice(room_choice);
    card.set_screen(s.canvas_w, s.canvas_h, output_of(s));
    card.set_local_player_name("Ana");
    if (report != nullptr) {
        card.set_report(*report);
        card.set_watch_available(true);                     // (the application says so when it has the recording)
    }
    card.show(result, local, mode);
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

bool has_line(const ArtSpy& spy, const std::string& text) { return spy.face(text) != nullptr; }

/// Is there a line that begins with `raids` and ends with `points` and a unit ("pts" or "point(s)") and is not cut (no three dots): the Top Thief's card in whichever wording fits
bool fitted_thief_line(const ArtSpy& spy, const std::string& raids, const std::string& points) {
    for (const ArtSpy::Face& f : spy.face_lines) {
        if (f.text.rfind(raids, 0) != 0 || f.text.find("...") != std::string::npos) continue;
        for (const char* unit : {" points", " point", " pts", " pt"}) {
            const std::string tail = points + unit;
            if (f.text.size() >= tail.size() && f.text.compare(f.text.size() - tail.size(), tail.size(), tail) == 0) return true;
        }
    }
    return false;
}

bool has_ink(const ArtSpy& spy, const std::string& text) {
    for (const ArtSpy::Line& line : spy.lines) {
        if (line.text == text) return true;
    }
    return false;
}

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
    sl::Podium podium;
    sl::Scale scale;
    LayoutRect leave, hub, link, watch;
    bool operator==(const Frozen& o) const {
        return podium.leave == o.podium.leave && podium.winner_well == o.podium.winner_well && podium.others_well == o.podium.others_well && podium.title_base == o.podium.title_base &&
               podium.play == o.podium.play && podium.honor[0] == o.podium.honor[0] && podium.narrow == o.podium.narrow && scale.k == o.scale.k && scale.w == o.scale.w && scale.h == o.scale.h &&
               leave == o.leave && hub == o.hub && link == o.link && watch == o.watch;
    }
};
Frozen frozen_of(const ScorecardModal& card) { return Frozen{card.podium(), card.scale(), card.leave_rect(), card.hub_rect(), card.link_rect(), card.watch_rect()}; }

bool same_events(const ArtSpy& a, const ArtSpy& b) {
    if (a.events.size() != b.events.size()) return false;
    for (size_t i = 0; i < a.events.size(); ++i) {
        const Spy::Ev &x = a.events[i], &y = b.events[i];
        if (x.kind != y.kind || x.name != y.name || x.x != y.x || x.y != y.y || x.w != y.w || x.h != y.h || x.sx != y.sx || x.sy != y.sy || x.sw != y.sw || x.sh != y.sh) return false;
    }
    return true;
}

void click(ScorecardModal& card, const LayoutRect& r) {
    card.handle_mouse_motion(r.x + r.w / 2, r.y + r.h / 2);
    card.handle_mouse_down(r.x + r.w / 2, r.y + r.h / 2);
    card.handle_mouse_up(r.x + r.w / 2, r.y + r.h / 2);
}

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
    check(count(*fresh_new, art, "panel_c") > 0 && count(*fresh_new, art, "sc_victory_26") == 1 && count(*fresh_new, art, "sc_leave_up") == 1 && count(*fresh_new, art, "sc_k_winner") == 1,
          "...the panel, the title, the winner's label and Leave Game are the art's");
    check(fresh_new->sheets.size() >= 2 && fresh_new->sheets.front() == dressed.scale().sheet && fresh_new->sheets.back() == 0, "...the sheet of the unit is chosen for it, and the choice is given back (" + std::to_string(fresh_new->sheets.size()) + " calls)");

    // the art taken away: the original's page, the same as if it had never been given
    dressed.set_art(nullptr);
    check(!dressed.new_look(), "the art taken away: the original's page again");
    const auto back = draw(arc, dressed, kWide720);
    check(same_events(*back, *classic) && back->copies.empty(), "...with every call that it made before the art was given");

    // the counted numbers are the new page's: the original's page does not show them, and is the same page whatever was counted
    ScorecardModal with_report;
    const MatchReport rep = counted();
    open(with_report, nullptr, kWide720, sample(), sim::GameMode::HighestScore, true, &rep);
    check(same_events(*draw(arc, with_report, kWide720), *classic), "the original's page with a report that was counted is the page without one, call for call");

    // a set that is not loaded, a canvas of fewer than 360 units: the original's page
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

void test_podium(const assets::AssetArchive& arc, const hud_art::Art& art) {
    group("podium", "the title, the sentence, the winner's well and every team's row are on the page");
    ScorecardModal card;
    open(card, &art, kWide720, sample());
    const Frozen before = frozen_of(card);
    const auto spy = draw(arc, card, kWide720);
    check(frozen_of(card) == before, "drawing a frame moves nothing: the layout was made when the rows were built");
    check(card.rows().size() == 3 && card.viewer_rank() == 0, "the sample has three rows: an alliance (the local team's) and two single teams");
    check(count(*spy, art, "sc_victory_26") == 1 && count(*spy, art, "sc_defeat_26") == 0, "the local team won: \"Victory!\"");
    const std::string win = card.rows()[0].name;
    check(win.find(" & ") != std::string::npos, "the first row is the alliance (" + win + ")");
    check(has_ink(*spy, win + " win the match."), "the sentence names the winners (an alliance win), with no time while nothing was counted");
    check(has_line(*spy, card.rows()[0].numbers[0]) && has_line(*spy, win), "the winner's name and score are drawn");
    check(has_line(*spy, "Lost") && has_line(*spy, "Killed") && has_line(*spy, "Hatched") && !has_line(*spy, "Damage"), "...with a tile for each of the others' numbers, and no damage: nothing was counted");
    const ArtSpy::Face* score = spy->face(card.rows()[0].numbers[0]);
    check(score != nullptr && score->color.r == 246 && score->color.g == 208, "the winner's score is gold");
    for (size_t i = 1; i < card.rows().size(); ++i) {
        const ScorecardModal::Row& row = card.rows()[i];
        check(has_line(*spy, row.numbers[0]), "row " + std::to_string(i) + ": the score " + row.numbers[0]);
        check(has_line(*spy, row.name), "row " + std::to_string(i) + ": the name \"" + row.name + "\"");
        check(spy->face("Lost " + row.numbers[1] + "   Killed " + row.numbers[2] + "   Hatched " + row.numbers[3]) != nullptr, "row " + std::to_string(i) + ": its three small numbers in a line");
    }
    check(count(*spy, art, "sc_well_tl") == 2 && count(*spy, art, "sc_others") == 1, "two wells, the winner's and the others', and the label of the others");
    int32_t ants = 0;
    for (const Spy::Ev& e : spy->events) ants += e.kind == Spy::Kind::Region ? 1 : 0;
    check(ants > 0, "the ants of the rows are the game's own pictures (" + std::to_string(ants) + " parts)");
    check(count(*spy, art, "sc_sm_m_up") == 0 && count(*spy, art, "sc_sw_allstats_up") == 0 && count(*spy, art, "sc_k_mvp") == 0 && count(*spy, art, "sc_watch_up") == 0, "nothing was counted: no link to a second page, no honors, no Watch");

    // the one that came second sees "Defeat" and its place, and its own row is marked
    ScorecardModal second;
    open(second, &art, kWide720, sample(), sim::GameMode::HighestScore, true, nullptr, 2);
    const auto spy2 = draw(arc, second, kWide720);
    check(second.viewer_rank() == 1 && count(*spy2, art, "sc_defeat_26") == 1 && count(*spy2, art, "sc_victory_26") == 0, "the local team's row is the second: \"Defeat\"");
    check(has_ink(*spy2, second.rows()[0].name + " win.   You came 2nd."), "...and the sentence says who won and \"You came 2nd.\"");
    check(has_line(*spy2, second.rows()[1].name + " (you)"), "...its row says \"(you)\"");

    // four separate teams
    ScorecardModal four;
    open(four, &art, kWide720, sample(false));
    const auto spy4 = draw(arc, four, kWide720);
    check(four.rows().size() == 4 && four.podium().other_count == 3, "four teams without an alliance have four rows");
    for (size_t i = 1; i < four.rows().size(); ++i) check(has_line(*spy4, four.rows()[i].name) && has_line(*spy4, four.rows()[i].numbers[0]), "four teams: " + four.rows()[i].name + " and its score are drawn");

    // a name that is too long ends in three dots, and stays in its room
    ScorecardModal longname;
    longname.set_player_names({"Bartholomew the Third of Antwerp, Duke", "Cy", "Dot", "Eve"});
    open(longname, &art, kWide720, sample(false));
    const auto spyl = draw(arc, longname, kWide720);
    bool dotted = false;
    for (const ArtSpy::Face& f : spyl->face_lines) dotted = dotted || (f.text.size() > 3 && f.text.compare(f.text.size() - 3, 3, "...") == 0 && f.text.find("Bartholomew") == 0);
    check(dotted, "a name that does not fit ends in three dots");
    for (const ArtSpy::Line& line : spyl->lines) {
        if (line.text.find("Bartholomew") != 0) continue;
        check(line.x + spyl->dialog_text_width(line.text, line.em_tenths) <= longname.scale().x(longname.podium().sentence_x + longname.podium().sentence_w), "...and the sentence that names it ends before its room does");
    }
    // a number of ten digits is set smaller and stays in its place
    sim::MatchResult big = sample(false);
    big.stats[0].score = 2000000000;
    ScorecardModal huge;
    open(huge, &art, kWide720, big);
    const auto spyh = draw(arc, huge, kWide720);
    const ArtSpy::Face* hf = spyh->face("2000000000");
    check(hf != nullptr && hf->em_tenths < huge.scale().em_tenths(34.0), "a number of ten digits is set smaller, not cut");
    // a name of accents is cut at a whole character: eight names, every place that a cut can fall
    int whole = 0;
    for (int extra = 0; extra < 8; ++extra) {
        ScorecardModal utf;
        const std::string pad(static_cast<size_t>(extra), 'x');
        utf.set_player_names({pad + "\xC3\x9C" "n\xC3\xAF" "c\xC3\xB6" "d\xC3\xA9 W\xC3\xA4" "rri\xC3\xB6" "r Qu\xC3\xA9\xC3\xA9" "n of the \xC3\x81" "ntz", pad + "\xC3\x89" "l\xC3\xA9" "onore \xC3\x89" "tienne \xC3\x86" "gir \xC3\x98" "rsted \xC3\x85" "sa \xC3\x9C" "nal \xC3\x87" "elik \xC3\x8F" "\xC3\xB1" "igo \xC3\x96" "zt\xC3\xBC" "rk Zo\xC3\xAB Bront\xC3\xAB", "Dot", "Eve"});
        open(utf, &art, kPhoneUp, sample(false));
        const auto spyu = draw(arc, utf, kPhoneUp);
        bool ok = !spyu->face_lines.empty() && !spyu->lines.empty();
        for (const ArtSpy::Face& f : spyu->face_lines) ok = ok && is_utf8(f.text);
        for (const ArtSpy::Line& line : spyu->lines) ok = ok && is_utf8(line.text);
        whole += ok ? 1 : 0;
    }
    check(whole == 8, "every line that is drawn is well formed UTF-8, whatever the names are made of (" + std::to_string(whole) + " of 8)");
}

void test_counted(const assets::AssetArchive& arc, const hud_art::Art& art) {
    group("counted", "a match whose numbers were counted has the damage, the honors, the Play of the game and a second page");
    ScorecardModal card;
    const MatchReport rep = counted();
    open(card, &art, kWide720, sample(), sim::GameMode::HighestScore, true, &rep);
    const Frozen before = frozen_of(card);
    const auto spy = draw(arc, card, kWide720);
    check(frozen_of(card) == before, "drawing the frame moves nothing");
    const std::string win = card.rows()[0].name;
    check(has_ink(*spy, win + " win the match.   12:40 played"), "the sentence says how long the match was");
    check(has_line(*spy, "Damage") && has_line(*spy, "1,840"), "the winner's well has the damage tile (the winner's side: 1,240 and 600, added up, with its thousands grouped)");
    check(spy->face("Lost " + card.rows()[1].numbers[1] + "   Killed " + card.rows()[1].numbers[2] + "   Hatched " + card.rows()[1].numbers[3] + "   Damage 420") != nullptr, "a ranked row's small numbers end with its damage");
    check(count(*spy, art, "sc_sm_m_up") == 1 && count(*spy, art, "sc_sw_allstats_up") == 1 && count(*spy, art, "sc_sw_back_up") == 0, "one link, \"All stats\", and no tabs");
    check(count(*spy, art, "sc_k_mvp") == 1 && count(*spy, art, "sc_k_blood") == 1 && count(*spy, art, "sc_k_thief") == 1 && count(*spy, art, "sc_k_stand") == 0 && count(*spy, art, "sc_k_play") == 1, "the labels of the three honors and of the Play of the game");
    check(has_line(*spy, "62% of the play") && has_line(*spy, "at 0:42") && fitted_thief_line(*spy, "3 raids", "100"), "the honors' numbers: the MVP's share of the play, First blood's time, the Top Thief's raids home and their points (the wording that fits the card)");
    check(has_line(*spy, card.seat_name(0)) && has_line(*spy, card.seat_name(2)), "...and who earned them");
    check(has_line(*spy, card.seat_name(1) + "'s bomb takes out") && has_line(*spy, "6 ants") && has_line(*spy, "at 7:41"), "the Play of the game says what happened and when (\"... bomb takes out\", \"6 ants\", \"at 7:41\")");
    check(count(*spy, art, "sc_watch_up") == 1, "Watch is there");
    // no still of the moment: the picture is the ants of the seat that made it, on a dark ground
    check(card.podium().play_picture.w > 0, "...with the picture's place");

    // a match without a Play of the game has no card for it, and no Watch
    ScorecardModal quiet;
    const MatchReport no_play = counted(false);
    open(quiet, &art, kWide720, sample(), sim::GameMode::HighestScore, true, &no_play);
    const auto spyq = draw(arc, quiet, kWide720);
    check(count(*spyq, art, "sc_k_play") == 0 && count(*spyq, art, "sc_watch_up") == 0 && count(*spyq, art, "sc_k_mvp") == 1 && quiet.watch_rect().w == 0, "no Play of the game: no card and no Watch, the honors stay");
    // no recording to open (the web build, a match of a room): no Watch button, the rest of the card stays
    ScorecardModal blind;
    open(blind, &art, kWide720, sample(), sim::GameMode::HighestScore, true, &rep);
    blind.set_watch_available(false);
    uint32_t blind_watched = 0;
    blind.set_on_watch([&](uint32_t, int32_t, int32_t) { ++blind_watched; });
    const auto spyw = draw(arc, blind, kWide720);
    click(blind, blind.scale().rect(blind.podium().watch));
    check(count(*spyw, art, "sc_watch_up") == 0 && blind.watch_rect().w == 0 && blind_watched == 0 && count(*spyw, art, "sc_k_play") == 1 && has_line(*spyw, "6 ants"), "no recording to open: the play is there, Watch is not, and a click where it would be does nothing");
    ScorecardModal sighted;
    open(sighted, &art, kWide720, sample(), sim::GameMode::HighestScore, true, &rep);
    sighted.set_watch_available(false);
    sighted.set_watch_available(true);
    check(sighted.watch_rect().w > 0 && count(*draw(arc, sighted, kWide720), art, "sc_watch_up") == 1, "...and Watch is there as soon as the application says that it can open the recording");
    // an honor that nobody earned says so
    MatchReport none = counted();
    none.mvp.present = false;
    ScorecardModal nobody;
    open(nobody, &art, kWide720, sample(), sim::GameMode::HighestScore, true, &none);
    check(has_line(*draw(arc, nobody, kWide720), "nobody"), "an honor that nobody earned says \"nobody\"");
    // the Top Thief's card says the raids that got home and their points, the longest wording that fits (a number is never cut): all of it where the cards are wide, shorter where they are narrow
    ScorecardModal wide_cards;
    open(wide_cards, &art, kUltra, sample(), sim::GameMode::HighestScore, true, &rep);
    check(has_line(*draw(arc, wide_cards, kUltra), "3 raids home \xC2\xB7 100 points"), "wide cards: \"3 raids home \xC2\xB7 100 points\"");
    MatchReport single = counted();
    single.top_thief = MatchReport::Honor{true, 2, 1, 0};
    single.raids_home = {0, 0, 1, 0};
    single.loot_home = {0, 0, 1, 0};
    ScorecardModal one_raid;
    open(one_raid, &art, kUltra, sample(), sim::GameMode::HighestScore, true, &single);
    const auto spy_one = draw(arc, one_raid, kUltra);
    check(has_line(*spy_one, "1 raid home \xC2\xB7 1 point") && has_line(*spy_one, one_raid.seat_name(2)), "one raid home is \"1 raid home\" and one point \"1 point\"");
    // a match in which none got home has the label over "nobody"
    MatchReport no_thief = counted();
    no_thief.top_thief.present = false;
    ScorecardModal no_raid;
    open(no_raid, &art, kWide720, sample(), sim::GameMode::HighestScore, true, &no_thief);
    const auto spy_none = draw(arc, no_raid, kWide720);
    check(count(*spy_none, art, "sc_k_thief") == 1 && has_line(*spy_none, "nobody") && !fitted_thief_line(*spy_none, "3 raids", "100"), "no raid got home: the Top Thief's label is there and the card says \"nobody\"");

    // the second page: the link shows it, every number of every seat, the best of a column in gold
    int jumps = 0;
    card.set_on_watch([&](uint32_t, int32_t, int32_t) { ++jumps; });
    click(card, card.link_rect());
    check(card.page() == ScorecardModal::Page::Stats && jumps == 0, "a click on \"All stats\" shows the second page");
    const auto page = draw(arc, card, kWide720);
    check(count(*page, art, "sc_matchstats_15") == 1 && count(*page, art, "sc_victory_26") == 0 && count(*page, art, "sc_sw_back_up") == 1 && count(*page, art, "sc_sw_allstats_up") == 0 && count(*page, art, "sc_sm_m_up") == 1,
          "the second page: its title, and the one link is \"Back to results\"");
    check(count(*page, art, "sc_g_damage") == 1 && count(*page, art, "sc_g_food") == 1 && count(*page, art, "sc_g_special") == 1 && count(*page, art, "sc_s_done") == 1 && count(*page, art, "sc_s_loot") == 1 && count(*page, art, "sc_s_trips") == 1, "...three groups of eight columns");
    check(count(*page, art, "sc_watch_up") == 0 && count(*page, art, "sc_k_mvp") == 0, "...and none of the first page's pieces");
    const ArtSpy::Face* done = page->face("1,240");
    const ArtSpy::Face* taken = page->face("1,350");
    check(done != nullptr && done->color.r == 246 && taken != nullptr && taken->color.r == 246, "the best of a column is gold (the most damage done, the most damage taken)");
    const ArtSpy::Face* second_best = page->face("600");
    check(second_best != nullptr && second_best->color.r == 244, "...the others are cream");
    for (size_t t = 0; t < 4; ++t) check(has_line(*page, card.seat_name(static_cast<uint8_t>(t))), "a row for every player: " + card.seat_name(static_cast<uint8_t>(t)));
    check(has_line(*page, "900") && has_line(*page, "150") && has_line(*page, "30"), "the food and the special ants of the first seat are in its row");
    // back to the results
    click(card, card.link_rect());
    check(card.page() == ScorecardModal::Page::Results && count(*draw(arc, card, kWide720), art, "sc_victory_26") == 1, "a click on \"Back to results\" shows the podium again");
    // the Watch button
    uint32_t watched = 0;
    int32_t watched_x = -1, watched_y = -1;
    card.set_on_watch([&](uint32_t turn, int32_t x, int32_t y) { watched = turn; watched_x = x; watched_y = y; ++jumps; });
    click(card, card.watch_rect());
    check(jumps == 1 && watched == rep.play.watch_turn() && watched == 9120 && watched_x == rep.play.tile_x && watched_y == rep.play.tile_y && watched_x == 10 && watched_y == 20, "Watch opens the replay five seconds before the moment (turn 9120 for turn 9220), with the view on its tile");

    // the second page again with the clicks that must do nothing there
    card.show_page(ScorecardModal::Page::Stats);
    check(card.watch_rect().w == 0, "the second page has no Watch");
    click(card, card.leave_rect());
    check(card.page() == ScorecardModal::Page::Stats, "...and Leave Game does not change the page");
    // a new screen starts at the podium
    card.show(sample(), 0);
    check(card.page() == ScorecardModal::Page::Results, "a screen that is shown again starts at the podium");

    // a match that was not counted: the original's numbers only, never a wrong one
    ScorecardModal bad;
    MatchReport failed = counted();
    failed.ok = false;
    open(bad, &art, kWide720, sample(), sim::GameMode::HighestScore, true, &failed);
    const auto spyb = draw(arc, bad, kWide720);
    check(count(*spyb, art, "sc_sm_m_up") == 0 && count(*spyb, art, "sc_k_mvp") == 0 && count(*spyb, art, "sc_k_play") == 0 && count(*spyb, art, "sc_watch_up") == 0 && !has_line(*spyb, "Damage") && !has_line(*spyb, "1,240"),
          "a report that is not ok: no link, no honors, no play, no damage");
    check(has_ink(*spyb, bad.rows()[0].name + " win the match.") && has_line(*spyb, bad.rows()[0].numbers[0]) && has_line(*spyb, "Lost") && has_line(*spyb, "Hatched"), "...the sentence has no time, and the original's four numbers are all there");
    bad.show_page(ScorecardModal::Page::Stats);
    check(bad.page() == ScorecardModal::Page::Results && bad.link_rect().w == 0, "...there is no second page to show");
    // the report that arrives after the screen opened is used at once, and one that is taken away is gone at once
    ScorecardModal late;
    open(late, &art, kWide720, sample());
    check(count(*draw(arc, late, kWide720), art, "sc_k_mvp") == 0, "before the report: nothing counted is shown");
    late.set_report(rep);
    check(count(*draw(arc, late, kWide720), art, "sc_k_mvp") == 1 && late.link_rect().w > 0, "...a report that was counted adds the link and the honors");
    late.show_page(ScorecardModal::Page::Stats);
    late.set_report(failed);
    check(late.page() == ScorecardModal::Page::Results && count(*draw(arc, late, kWide720), art, "sc_sm_m_up") == 0, "...and one that was not takes the second page away");
}

void test_waiting(const assets::AssetArchive& arc, const hud_art::Art& art) {
    group("waiting", "the waiting page: \"Game Over\" and one line, with Leave Game from the first moment");
    ScorecardModal card;
    const MatchReport rep = counted();
    open(card, &art, kWide720, sample(), sim::GameMode::HighestScore, false, &rep);
    int quits = 0;
    card.set_on_quit([&]() { ++quits; });
    const Frozen before = frozen_of(card);
    const auto spy = draw(arc, card, kWide720);
    check(card.is_waiting() && count(*spy, art, "sc_gameover_36") == 1 && has_ink(*spy, "The match has ended. Adding up the scores..."), "the waiting screen has \"Game Over\" and its line");
    check(count(*spy, art, "sc_well_tl") == 0 && count(*spy, art, "sc_k_winner") == 0 && count(*spy, art, "sc_sm_m_up") == 0 && count(*spy, art, "sc_leave_up") == 1, "...no empty wells, no link, and Leave Game");
    check(frozen_of(card) == before, "drawing the waiting frame moves nothing");
    click(card, card.leave_rect());
    check(quits == 1, "a click on Leave Game leaves at once, before the scores are in");
    card.update(0.25f);
    const auto rows = draw(arc, card, kWide720);
    check(!card.is_waiting() && count(*rows, art, "sc_gameover_36") == 0 && count(*rows, art, "sc_victory_26") == 1 && count(*rows, art, "sc_sw_allstats_up") == 1, "a quarter of a second later: the podium, with the link");
    // the narrow page's waiting page has the big button
    ScorecardModal phone;
    open(phone, &art, kPhoneUp, sample(), sim::GameMode::HighestScore, false);
    const auto spyp = draw(arc, phone, kPhoneUp);
    check(count(*spyp, art, "sc_gameover_26") == 1 && count(*spyp, art, "sc_btn_m_up") >= 1 && count(*spyp, art, "sc_leave_word_up") == 1, "a phone held upright: the smaller \"Game Over\" and the big button");
}

void test_187(const assets::AssetArchive& arc, const hud_art::Art& art) {
    group("187", "the headline is the sentence, two tiles, Last stand, six columns");
    ScorecardModal card;
    sim::MatchResult both = sample();
    both.standing = 0;
    both.ants_left = {5, 4, 0, 0};
    const MatchReport rep = counted();
    open(card, &art, kWide720, both, sim::GameMode::Kills187, true, &rep);
    const Frozen before = frozen_of(card);
    const auto spy = draw(arc, card, kWide720);
    check(frozen_of(card) == before, "drawing the frame moves nothing");
    check(!card.headline().empty() && has_ink(*spy, card.headline() + "   12:40 played"), "the headline is the sentence, and how long it took (\"" + card.headline() + "\")");
    check(count(*spy, art, "sc_k_colony") == 1 && count(*spy, art, "sc_k_winner") == 0, "the winner's label is \"Last colony\"");
    check(has_line(*spy, "Lost") && has_line(*spy, "Left") && !has_line(*spy, "Killed") && !has_line(*spy, "Hatched") && has_line(*spy, "Damage"), "two tiles for the numbers (lost, left) and the damage");
    check(count(*spy, art, "sc_k_stand") == 1 && count(*spy, art, "sc_k_thief") == 0 && has_line(*spy, "out of ants at 2:10"), "the honors are MVP, First blood and Last stand (no Top Thief)");
    check(spy->face("Lost " + card.rows()[1].numbers[1] + "   Left " + card.rows()[1].numbers[2] + "   Damage 420") != nullptr, "a ranked row has its numbers, without the hatched");
    click(card, card.link_rect());
    const auto page = draw(arc, card, kWide720);
    check(count(*page, art, "sc_g_damage") == 1 && count(*page, art, "sc_g_food") == 0 && count(*page, art, "sc_g_special") == 1 && count(*page, art, "sc_s_trips") == 0 && count(*page, art, "sc_s_done") == 1 && count(*page, art, "sc_s_loot") == 1,
          "the second page of 187: damage and special ants, no food");
    // no headline when a quit ended the match: the plain sentence
    ScorecardModal quit;
    sim::MatchResult quitter = sample();
    quitter.quitter = 3;
    open(quit, &art, kWide720, quitter, sim::GameMode::Kills187, true, &rep);
    check(quit.headline().empty() && has_ink(*draw(arc, quit, kWide720), quit.rows()[0].name + " win the match.   12:40 played"), "a match that a quit ended has no headline: the sentence is the plain one");
    // a headline of long names with accents is cut at the end of its room, at whole characters (a name a letter longer moves the cut by a byte: eight of them cover every place a cut can fall)
    int cut_whole = 0;
    for (int extra = 0; extra < 8; ++extra) {
        ScorecardModal utf;
        const std::string pad(static_cast<size_t>(extra), 'x');
        utf.set_player_names({pad + "\xC3\x9C" "n\xC3\xAF" "c\xC3\xB6" "d\xC3\xA9 W\xC3\xA4" "rri\xC3\xB6" "r Qu\xC3\xA9\xC3\xA9" "n of the \xC3\x81" "ntz", "\xC3\x89" "l\xC3\xA9" "onore \xC3\x89" "tienne \xC3\x86" "gir \xC3\x98" "rsted \xC3\x85" "sa", "Dot", "Eve"});
        open(utf, &art, kPhoneUp, both, sim::GameMode::Kills187);
        const auto spyu = draw(arc, utf, kPhoneUp);
        bool ok = false;
        for (const ArtSpy::Line& line : spyu->lines) ok = ok || (line.text.size() > 3 && line.text.compare(line.text.size() - 3, 3, "...") == 0 && is_utf8(line.text) && line.text.find("last colon") == std::string::npos);
        cut_whole += ok ? 1 : 0;
    }
    check(cut_whole == 8, "a headline that is too long for its room ends in three dots after a whole character, whatever the names are made of (" + std::to_string(cut_whole) + " of 8)");
    // a long headline is cut at the end of the room, at whole characters
    ScorecardModal narrow;
    narrow.set_player_names({"Bartholomew the Third of Antwerp", "Cornelius the Mighty Builder of Hills", "Dot", "Eve"});
    open(narrow, &art, kPhoneUp, both, sim::GameMode::Kills187);
    const auto spyn = draw(arc, narrow, kPhoneUp);
    bool cut = false;
    for (const ArtSpy::Line& line : spyn->lines) cut = cut || (line.text.size() > 3 && line.text.compare(line.text.size() - 3, 3, "...") == 0 && is_utf8(line.text));
    check(cut, "a long headline on a phone is cut with three dots, to the room of the sentence");
}

void test_reflow(const assets::AssetArchive& arc, const hud_art::Art& art) {
    group("reflow", "a narrow picture stacks the page in one column");
    ScorecardModal card;
    const MatchReport rep = counted();
    open(card, &art, kPhoneUp, sample(), sim::GameMode::HighestScore, true, &rep);
    const auto spy = draw(arc, card, kPhoneUp);
    check(card.new_look() && card.podium().narrow && card.scale().k == 3, "a phone held upright: one column, at 3 device pixels to the unit");
    check(count(*spy, art, "sc_victory_22") == 1 && count(*spy, art, "sc_leave_up") == 0 && count(*spy, art, "sc_btn_l_up") == 1 && count(*spy, art, "sc_btn_r_up") == 1 && count(*spy, art, "sc_btn_m_up") >= 1 && count(*spy, art, "sc_leave_word_up") == 1,
          "...the smaller title, and Leave Game as the big button: its ends, its run and its word");
    check(count(*spy, art, "sc_sm_l_up") == 1 && count(*spy, art, "sc_sm_r_up") == 1 && count(*spy, art, "sc_sw_allstats_up") == 1, "...the link \"All stats\", one strip over the column");
    const LayoutRect big = card.leave_rect();
    check(big.w > card.scale().canvas_w * 8 / 10 && card.link_rect().w > card.scale().canvas_w * 8 / 10, "the big button and the link are most of the picture wide");
    check(card.podium().play.w > 0 && count(*spy, art, "sc_watch_up") == 1, "...the Play of the game and Watch are there");
    click(card, card.link_rect());
    const auto page = draw(arc, card, kPhoneUp);
    check(card.page() == ScorecardModal::Page::Stats && count(*page, art, "sc_g_damage") == 0 && has_line(*page, "Dmg done") && has_line(*page, "Loot"), "the second page of a narrow picture is cards: a little label under each number");

    // 640 x 480 at 150% is 960 x 720 units at 1: two columns, the corner's button
    ScorecardModal small;
    open(small, &art, kSmall43, sample());
    const auto spys = draw(arc, small, kSmall43);
    check(small.new_look() && small.scale().k == 1 && small.scale().w == 960 && small.scale().h == 720 && !small.podium().narrow, "640 x 480 at 150%: 960 x 720 units at 1, a page of two columns");
    check(count(*spys, art, "sc_leave_up") == 1 && count(*spys, art, "sc_btn_m_up") == 0, "...with the corner's button");
    ScorecardModal side;
    open(side, &art, kPhoneSide, sample());
    check(side.new_look() && !side.podium().narrow, "a phone held sideways: two columns");
    ScorecardModal ultra;
    open(ultra, &art, kUltra, sample());
    check(ultra.new_look() && ultra.scale().k == 4 && ultra.podium().content.w <= sl::kMaxContent, "an ultrawide: the column is no wider than 760 units, centered");
    check(ultra.podium().content.x * 2 + ultra.podium().content.w == ultra.scale().w || ultra.podium().content.x * 2 + ultra.podium().content.w == ultra.scale().w - 1, "...in the middle");
}

void test_buttons(const assets::AssetArchive& arc, const hud_art::Art& art) {
    group("buttons", "Leave Game in its three states, the link to the other page and Watch");
    ScorecardModal card;
    const MatchReport rep = counted();
    open(card, &art, kWide720, sample(), sim::GameMode::HighestScore, true, &rep);
    int quits = 0;
    card.set_on_quit([&]() { ++quits; });
    int clicks = 0;
    card.set_on_play_sfx([&](uint32_t) { ++clicks; });
    const LayoutRect b = card.leave_rect();
    check(b.w > 90 && b.h > 30, "the button's face is " + str(b) + " canvas pixels");
    check(b == card.scale().rect(card.podium().leave), "...the face of the layout's rectangle, in canvas pixels");
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
    card.leave();
    check(quits == 3, "Enter, C, Q and X leave at any time (the application calls leave())");

    // the link and Watch: a state each, a click sound, an action at the release only
    const LayoutRect link = card.link_rect();
    check(link.w > 60 && link == card.scale().rect(card.podium().link), "on the podium the link \"All stats\" is a button (" + str(link) + ")");
    const int sounds = clicks;
    card.handle_mouse_motion(link.x + 3, link.y + 3);
    check(count(*draw(arc, card, kWide720), art, "sc_sw_allstats_hover") == 1, "the link pointed at: its hover word");
    card.handle_mouse_down(link.x + 3, link.y + 3);
    check(count(*draw(arc, card, kWide720), art, "sc_sw_allstats_down") == 1 && clicks == sounds + 1 && card.page() == ScorecardModal::Page::Results, "pressed: its pressed word, the click's sound, and the page has not changed");
    card.handle_mouse_motion(link.x + link.w + 30, link.y + 3);
    card.handle_mouse_up(link.x + link.w + 30, link.y + 3);
    check(card.page() == ScorecardModal::Page::Results, "released away from the link: the page stays");
    card.handle_mouse_down(link.x + link.w, link.y + 3);
    card.handle_mouse_up(link.x + link.w, link.y + 3);
    check(card.page() == ScorecardModal::Page::Results, "the pixel right of the face is not the link");
    card.handle_mouse_down(link.x + link.w - 1, link.y + link.h - 1);
    card.handle_mouse_up(link.x + link.w - 1, link.y + link.h - 1);
    check(card.page() == ScorecardModal::Page::Stats, "released on it (its last pixel): the second page");
    const LayoutRect back = card.link_rect();
    check(back.x == link.x && back.y == link.y && back.h == link.h && back.w > link.w && back == card.scale().rect(card.stats_layout().link), "on the second page \"Back to results\" is in the same place, as high, and wider (" + str(back) + ")");
    check(card.leave_rect() == b, "...and Leave Game has not moved");
    card.handle_mouse_motion(back.x + 3, back.y + 3);
    check(count(*draw(arc, card, kWide720), art, "sc_sw_back_hover") == 1, "\"Back to results\" pointed at: its hover word");
    click(card, back);
    check(card.page() == ScorecardModal::Page::Results, "a click on it shows the podium again");
    card.show_page(ScorecardModal::Page::Stats);
    card.show_page(ScorecardModal::Page::Stats);
    check(card.page() == ScorecardModal::Page::Stats, "asking for the second page twice is the second page");
    card.show_page(ScorecardModal::Page::Results);
    check(card.page() == ScorecardModal::Page::Results && card.link_target() == ScorecardModal::Page::Stats, "...and the link leads to the other one");

    // the big button of a narrow page
    ScorecardModal phone;
    open(phone, &art, kPhoneUp, sample());
    int phone_quits = 0;
    phone.set_on_quit([&]() { ++phone_quits; });
    const LayoutRect pb = phone.leave_rect();
    phone.handle_mouse_down(pb.x + pb.w / 2, pb.y + pb.h / 2);
    const auto pd = draw(arc, phone, kPhoneUp);
    check(count(*pd, art, "sc_btn_m_down") >= 1 && count(*pd, art, "sc_leave_word_down") == 1, "the big button, pressed: its pressed ends, run and word");
    phone.handle_mouse_up(pb.x + pb.w / 2, pb.y + pb.h / 2);
    check(phone_quits == 1, "...and it leaves");
    phone.hide();
    phone.handle_mouse_down(pb.x + 5, pb.y + 5);
    phone.handle_mouse_up(pb.x + 5, pb.y + 5);
    check(phone_quits == 1, "a closed screen takes no click");
}

/// The match of a lobby room: the two buttons that stand where Leave Game stands, on every page, and the screen that waits for one of them
void test_room(const assets::AssetArchive& arc, const hud_art::Art& art) {
    group("room", "the match of a lobby room: \"Back to the room\" and \"Back to Ants & Uncles\" in place of Leave Game, on every page, until one is pressed");
    const MatchReport rep = counted();
    ScorecardModal card;
    open(card, &art, kWide720, sample(), sim::GameMode::HighestScore, true, &rep, 0, true);
    int quits = 0, rooms = 0, hubs = 0, clicks = 0;
    card.set_on_quit([&]() { ++quits; });
    card.set_on_back_to_room([&]() { ++rooms; });
    card.set_on_back_to_hub([&]() { ++hubs; });
    card.set_on_play_sfx([&](uint32_t) { ++clicks; });
    check(card.room_choice(), "the new page of a lobby room's match has the choice");
    const auto spy = draw(arc, card, kWide720);
    check(count(*spy, art, "sc_leave_up") == 0 && count(*spy, art, "sc_sw_room_up") == 1 && count(*spy, art, "sc_sw_hub_up") == 1 && count(*spy, art, "sc_sm_l_up") == 3 && count(*spy, art, "sc_sm_r_up") == 3,
          "...no Leave Game: \"Back to the room\" and \"Back to Ants & Uncles\" are two buttons of the art's small strip (with the link, three)");
    const LayoutRect room = card.leave_rect(), hub = card.hub_rect(), link = card.link_rect();
    check(room == card.scale().rect(card.podium().leave) && hub == card.scale().rect(card.podium().hub) && room.w > 0 && hub.w > room.w && hub.y == room.y && hub.x > room.right() + 8 && link.right() < room.x,
          "...the rooms' face is where Leave Game stood, the other button right of it, the link left of both (" + str(room) + " " + str(hub) + " " + str(link) + ")");
    check(hub.right() <= card.scale().canvas_w, "...all of it on the picture");
    // each button does its own thing, once, at the release on it, with the click's sound
    click(card, room);
    check(rooms == 1 && hubs == 0 && quits == 0 && clicks == 1, "a click on \"Back to the room\" goes back to the room");
    click(card, hub);
    check(rooms == 1 && hubs == 1 && quits == 0 && clicks == 2, "a click on \"Back to Ants & Uncles\" goes to the front page, and nothing else happens");
    card.handle_mouse_motion(hub.x + 3, hub.y + 3);
    card.handle_mouse_down(hub.x + 3, hub.y + 3);
    const auto pressed = draw(arc, card, kWide720);
    check(count(*pressed, art, "sc_sw_hub_down") == 1 && count(*pressed, art, "sc_sw_room_up") == 1 && hubs == 1, "pressed: that button's pressed picture, the other still resting, and nothing happens yet");
    card.handle_mouse_motion(room.x + 3, room.y + 3);
    card.handle_mouse_up(room.x + 3, room.y + 3);
    check(hubs == 1 && rooms == 1, "pressed on one and released on the other: neither");
    card.handle_mouse_motion(room.x + 3, room.y + 3);
    check(count(*draw(arc, card, kWide720), art, "sc_sw_room_hover") == 1 && count(*draw(arc, card, kWide720), art, "sc_sw_hub_up") == 1, "pointed at: that button's hover picture");
    card.handle_mouse_down(room.right(), room.y + 3);
    card.handle_mouse_up(room.right(), room.y + 3);
    card.handle_mouse_down(hub.x - 1, hub.y + 3);
    card.handle_mouse_up(hub.x - 1, hub.y + 3);
    check(rooms == 1 && hubs == 1, "the pixel right of the first button's face and the one left of the second's are neither");
    card.handle_mouse_down(hub.right() - 1, hub.bottom() - 1);
    card.handle_mouse_up(hub.right() - 1, hub.bottom() - 1);
    check(hubs == 2, "the last pixel of the second button's face is it");
    card.leave();
    check(rooms == 2 && hubs == 2 && quits == 0, "the keys Enter, C, Q and X take the way that keeps the seat: back to the room (and never Leave Game's)");

    // no timer: the screen waits for a button, as long as it takes
    card.handle_mouse_motion(2, 2);
    for (int i = 0; i < 100; ++i) card.update(60.0f);
    check(card.is_open() && rooms == 2 && hubs == 2 && quits == 0, "a hundred minutes later nothing was pressed for the player and the screen is still there");
    check(count(*draw(arc, card, kWide720), art, "sc_sw_room_up") == 1 && count(*draw(arc, card, kWide720), art, "sc_sw_hub_up") == 1, "...with both buttons");

    // the second page has them in the same place
    click(card, link);
    check(card.page() == ScorecardModal::Page::Stats && card.leave_rect() == room && card.hub_rect() == hub, "on the second page the two buttons have not moved");
    const auto stats = draw(arc, card, kWide720);
    check(count(*stats, art, "sc_sw_room_up") + count(*stats, art, "sc_sw_room_hover") == 1 && count(*stats, art, "sc_sw_hub_up") == 1 && count(*stats, art, "sc_leave_up") == 0, "...and are drawn there");
    click(card, hub);
    check(hubs == 3, "...and work there");
    click(card, card.link_rect());
    check(card.page() == ScorecardModal::Page::Results, "...the way back to the results is the same link");

    // from the first moment: the waiting page has them too
    ScorecardModal waiting;
    open(waiting, &art, kWide720, sample(), sim::GameMode::HighestScore, false, &rep, 0, true);
    int waiting_hubs = 0;
    waiting.set_on_back_to_hub([&]() { ++waiting_hubs; });
    const auto spyw = draw(arc, waiting, kWide720);
    check(waiting.is_waiting() && count(*spyw, art, "sc_sw_room_up") == 1 && count(*spyw, art, "sc_sw_hub_up") == 1 && count(*spyw, art, "sc_leave_up") == 0, "the waiting page has the two buttons from the first moment");
    click(waiting, waiting.hub_rect());
    check(waiting_hubs == 1, "...and they work before the scores are in");

    // a narrow page stacks them as the big buttons, the room's on top
    ScorecardModal phone;
    open(phone, &art, kPhoneUp, sample(), sim::GameMode::HighestScore, true, &rep, 0, true);
    int phone_rooms = 0, phone_hubs = 0;
    phone.set_on_back_to_room([&]() { ++phone_rooms; });
    phone.set_on_back_to_hub([&]() { ++phone_hubs; });
    const auto spyp = draw(arc, phone, kPhoneUp);
    const LayoutRect pr = phone.leave_rect(), ph = phone.hub_rect();
    check(count(*spyp, art, "sc_bw_room_up") == 1 && count(*spyp, art, "sc_bw_hub_up") == 1 && count(*spyp, art, "sc_leave_word_up") == 0 && count(*spyp, art, "sc_btn_l_up") == 2 && count(*spyp, art, "sc_sw_allstats_up") == 1,
          "a phone held upright: two big buttons with their words, and the link");
    check(pr.x == ph.x && pr.w == ph.w && ph.y > pr.bottom() + 8 && pr.w > phone.scale().canvas_w * 8 / 10 && ph.bottom() < phone.scale().canvas_h, "...one over the other, a column wide, the room's on top (" + str(pr) + " " + str(ph) + ")");
    click(phone, ph);
    click(phone, pr);
    click(phone, LayoutRect{pr.x, ph.bottom() + 1, pr.w, 2});
    check(phone_rooms == 1 && phone_hubs == 1, "...each is its own button, and the clay between and under them is none");
    ScorecardModal phone_stats;
    open(phone_stats, &art, kPhoneUp, sample(), sim::GameMode::HighestScore, true, &rep, 0, true);
    phone_stats.show_page(ScorecardModal::Page::Stats);
    check(phone_stats.page() == ScorecardModal::Page::Stats && phone_stats.leave_rect().w > 0 && phone_stats.hub_rect().w > 0 && phone_stats.hub_rect().y > phone_stats.leave_rect().bottom(), "the second page of a narrow picture has them too");

    // a game on this computer keeps Leave Game, and so does the original's page
    ScorecardModal local;
    open(local, &art, kWide720, sample(), sim::GameMode::HighestScore, true, &rep);
    int local_quits = 0, local_rooms = 0;
    local.set_on_quit([&]() { ++local_quits; });
    local.set_on_back_to_room([&]() { ++local_rooms; });
    const auto spyl = draw(arc, local, kWide720);
    check(!local.room_choice() && local.hub_rect().w == 0 && count(*spyl, art, "sc_leave_up") == 1 && count(*spyl, art, "sc_sw_room_up") == 0 && count(*spyl, art, "sc_sw_hub_up") == 0, "a game on this computer: Leave Game, and no other button");
    click(local, local.leave_rect());
    local.leave();
    check(local_quits == 2 && local_rooms == 0, "...Leave Game and the keys leave the game, as ever");
    local.set_room_choice(true);
    check(local.room_choice() && local.hub_rect().w > 0 && count(*draw(arc, local, kWide720), art, "sc_leave_up") == 0, "the choice, set for the next match, replaces it");
    local.set_room_choice(false);
    check(!local.room_choice() && local.hub_rect().w == 0 && count(*draw(arc, local, kWide720), art, "sc_leave_up") == 1, "...and taken away again, Leave Game is back");
    ScorecardModal classic;
    open(classic, nullptr, kWide720, sample(), sim::GameMode::HighestScore, true, &rep, 0, true);
    int classic_quits = 0, classic_rooms = 0;
    classic.set_on_quit([&]() { ++classic_quits; });
    classic.set_on_back_to_room([&]() { ++classic_rooms; });
    classic.leave();
    check(!classic.room_choice() && classic_quits == 1 && classic_rooms == 0, "the original's page has Leave Game only: the keys and the button leave, whatever the match was");
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
    std::vector<LayoutRect> masked;
    const auto window_of = [&](const LayoutRect& units_rect) {
        if (units_rect.w <= 0 || units_rect.h <= 0) return;
        const LayoutRect c = card.scale().rect(units_rect);
        masked.push_back(LayoutRect{static_cast<int32_t>(std::floor(c.x * o)), static_cast<int32_t>(std::floor(c.y * o)), static_cast<int32_t>(std::ceil(c.w * o)) + 1, static_cast<int32_t>(std::ceil(c.h * o)) + 1});
    };
    const auto well_of = [&](const LayoutRect& g) { window_of(LayoutRect{g.x - 4, g.y - 4, g.w + 10, g.h + 10}); };
    if (card.page() == ScorecardModal::Page::Stats && !card.is_waiting()) {
        well_of(card.stats_layout().well);
        for (const char* name : {"sc_s_done", "sc_s_taken", "sc_s_trips", "sc_s_points", "sc_s_bombs", "sc_s_fires", "sc_s_raids", "sc_s_loot"}) {
            const hud_art::Sprite* sp = art.sprite_at(art.find_sprite(name));
            if (sp != nullptr) window_of(LayoutRect{0, card.stats_layout().sub_base - sp->y, card.scale().w, sp->h});
        }
    } else if (!card.is_waiting()) {
        const sl::Podium& p = card.podium();
        well_of(p.winner_well);
        well_of(p.others_well);
        if (p.play.w > 0) well_of(p.play);
        if (p.honors) {
            for (const LayoutRect& h : p.honor) well_of(h);
        }
    }
    int64_t black = 0;
    for (int32_t y = 0; y < fit.viewport.h; ++y) {
        for (int32_t x = 0; x < fit.viewport.w; ++x) {
            bool in_mask = false;
            for (const LayoutRect& w : masked) in_mask = in_mask || w.contains(x, y);
            const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(ww) + static_cast<size_t>(x)) * 4u;
            if (!in_mask && px[i] == 0 && px[i + 1] == 0 && px[i + 2] == 0) ++black;
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
    // the original's page is what it was after the art was given and taken away, to the byte, and a report changes nothing of it
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
        dressed.set_report(counted());
        dressed.show(sample(), 0);
        dressed.update(0.25f);
        check(dressed.new_look(), "the art is given");
        const std::vector<uint8_t> with = frame_of(rig, arc, dressed);
        dressed.set_art(nullptr);
        const std::vector<uint8_t> after = frame_of(rig, arc, dressed);
        int64_t differ = 0;
        for (size_t i = 0; i < before.size(); ++i) differ += before[i] != with[i] ? 1 : 0;
        check(!before.empty() && before == after, "the original's page, after the art is taken away and with a report that was counted, is the page that was never dressed: the same bytes");
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
        card.set_report(counted());
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
        const LayoutRect at_units = card.podium().leave;
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
        // the second page and the waiting page cover the picture too
        card.show_page(ScorecardModal::Page::Stats);
        const int64_t black2 = black_behind(frame_of(rig, arc, card), sz.w, sz.h, sz.w, sz.h, card, art);
        check(black2 == 0, at + "the second page covers the whole picture (" + std::to_string(black2) + ")");
        ScorecardModal waiting;
        waiting.set_art(&art);
        waiting.set_screen(sz.w, sz.h, 1.0);
        waiting.show(sample(), 0);
        const int64_t black3 = black_behind(frame_of(rig, arc, waiting), sz.w, sz.h, sz.w, sz.h, waiting, art);
        check(black3 == 0, at + "the waiting page covers the whole picture (" + std::to_string(black3) + ")");
    }
    // a unit of 4 (1440p) and a window that is not a multiple of the canvas: the picture is covered, and Leave Game shows
    {
        const Screen s{"1440p", 960, 540, 2560, 1440};
        Rig rig(arc, art, s.canvas_w, s.canvas_h, s.window_w, s.window_h);
        ScorecardModal card;
        const MatchReport rep = counted();
        open(card, &art, s, sample(), sim::GameMode::HighestScore, true, &rep);
        const std::vector<uint8_t> px = frame_of(rig, arc, card);
        const int64_t black = black_behind(px, s.canvas_w, s.canvas_h, s.window_w, s.window_h, card, art);
        check(card.scale().k == 4 && card.scale().sheet == 3 && black <= black_allowed(s), "1440p: a unit of 4 on the 3x sheet, the panel covers the picture (" + std::to_string(black) + " black pixels)");
        const LayoutRect b = card.leave_rect();
        const double o = output_of(s);
        const size_t mid = (static_cast<size_t>(std::lround((b.y + b.h / 2) * o)) * static_cast<size_t>(s.window_w) + static_cast<size_t>(std::lround((b.x + b.w / 2) * o))) * 4u;
        check(px[mid + 1] > px[mid + 0] && px[mid + 1] < 140, "...the button's face is green steel where the pointer would be");
    }
    // the new page differs between a 4:3 canvas and a 21:9 one only by what they have room for: both are covered
    for (const Screen& s : {kUltra, kPhoneSide, kPhoneUp, kSmall43}) {
        Rig rig(arc, art, s.canvas_w, s.canvas_h, s.window_w, s.window_h);
        ScorecardModal card;
        const MatchReport rep = counted();
        open(card, &art, s, sample(), sim::GameMode::HighestScore, true, &rep);
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
        rig.app.set_page_exits([]() { return true; }, []() {});                          // (a page that has both exits: a game on this computer has no room to go back to)
        rig.results();
        const ScorecardModal& card = rig.app.scorecard();
        check(card.is_open() && !card.is_waiting() && card.new_look() && rig.app.picture() == canvas.rect(), at + "New art: the results are the new page and the picture is the whole canvas " + str(rig.app.picture()));
        check(!rig.app.room_choice_applies() && !card.room_choice() && card.hub_rect().w == 0, at + "a game on this computer keeps Leave Game, whatever the page offers");
        const LayoutRect b = card.leave_rect();
        rig.click(b.x - 30, b.y + b.h / 2);
        check(quits == 0, at + "a click beside the button does nothing");
        rig.click(b.x + b.w / 2, b.y + b.h / 2);
        check(quits == 1, at + "a click on the button leaves");
    }
    // the 4:3 picture in a window of 960 x 720: 960 x 720 units at 1, two columns, and the click on the corner's button
    {
        AppRig rig(Aspect::Classic4x3, true, 1.5);
        check(rig.ok && rig.app.start_game("Original-Ants/Maps/SMALL.LVL"), "4:3 at 150%: a match starts");
        int quits = 0;
        rig.app.scorecard().set_on_quit([&]() { ++quits; });
        rig.results();
        const ScorecardModal& card = rig.app.scorecard();
        check(card.is_open() && card.new_look() && !card.podium().narrow && card.scale().w == 960 && card.scale().h == 720 && rig.app.picture() == CanvasLayout::of(Aspect::Classic4x3).rect(),
              "4:3 at 150%: the results are the two columns' page, 960 x 720 units, and the picture is the whole canvas " + str(rig.app.picture()));
        const LayoutRect b = card.leave_rect();
        check(b.w > 40 && b.bottom() <= 480, "...Leave Game is inside the picture " + str(b));
        rig.click(b.x + b.w / 2, b.bottom() + 12);
        check(quits == 0, "...a click under the button does nothing");
        rig.click(b.x + b.w / 2, b.y + b.h / 2);
        check(quits == 1, "...a click on the button leaves");
    }
    // the counted numbers reach the screen, and Watch opens the recording of the game just played (a game against a computer player on TINY: the quit that ends it)
    {
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.start_in_map_select = true;
        cfg.default_map_path = "Original-Ants/Maps/TINY.LVL";
        cfg.play_at_once = true;
        cfg.lan_port = 0;
        ants::ai::BotSpec bot;
        bot.seat = 1;
        bot.level = ants::ai::Level::Medium;
        cfg.bots.push_back(bot);
        cfg.hud_art_given = true;
        cfg.hud_art_new = true;
        cfg.hud_art_dir = art_folder();
        Application app;
        bool up = false;
        {
            QuietStdout quiet;
            up = app.init(cfg);
        }
        check(up && app.recorder() != nullptr, "Watch: a game against a computer player starts, and is recorded");
        app.hud().dismiss_match_start_modal();
        for (int i = 0; i < 400; ++i) app.update_simulation(0.05f);
        const auto key = [&](SDL_Keycode sym, uint16_t mod) {
            SDL_KeyboardEvent ev{};
            ev.type = SDL_KEYDOWN;
            ev.keysym.sym = sym;
            ev.keysym.mod = mod;
            app.handle_key_down(ev);
        };
        key(SDLK_q, KMOD_LCTRL);                                                        // the quit that ends a match with one other side left
        key(SDLK_y, 0);
        check(app.sim().is_match_over() && app.scorecard().is_open() && !app.last_replay().empty(), "the match ended, the results are open and its recording is kept");
        ScorecardModal& card = app.scorecard();
        check(app.match_report().ok && card.report().ok && card.report().turns == app.sim().current_tick() && card.report().turns > 300, "the results have the report of the match that was counted (" + std::to_string(card.report().turns) + " turns)");
#if !defined(__EMSCRIPTEN__)
        check(card.watch_available() && app.can_watch_play(), "a game on this computer that has its recording: Watch can be offered");
#endif
        const MatchReport live = app.match_report();                                     // (what was counted: the viewer's end page has it again)
        MatchReport shown = counted();                                                  // (a Play of the game at turn 300: Watch opens turn 200)
        shown.turns = card.report().turns;
        shown.play.turn = 300;
        const sim::WorldState& map = app.sim().get_world_state();
        shown.play.tile_x = static_cast<int32_t>(map.width / 2);                        // (a moment in the middle of the map)
        shown.play.tile_y = static_cast<int32_t>(map.height / 2);
        card.set_report(shown);
        app.update_results(0.3f);
        key(SDLK_RIGHT, 0);
        check(card.page() == ScorecardModal::Page::Stats && card.report().ok, "the right arrow (the link's own) shows the second page");
        key(SDLK_RIGHT, 0);
        check(card.page() == ScorecardModal::Page::Stats, "...again: it stays there");
        key(SDLK_LEFT, 0);
        check(card.page() == ScorecardModal::Page::Results && card.is_open(), "the left arrow shows the results again");
        key(SDLK_LEFT, 0);
        check(card.page() == ScorecardModal::Page::Results && card.is_open(), "...again: it stays there");
        const LayoutRect watch = card.watch_rect();
        check(card.new_look() && watch.w > 0, "the Play of the game has its Watch button on the screen");
        click(card, watch);
        check(app.replay_mode() && !card.is_open(), "a click on Watch closes the results and starts the recording in the viewer");
        check(app.replay_value(ReplayValue::JumpTarget) == 200, "...it jumps to five seconds before the moment (turn 200 for turn 300)");
        for (int i = 0; i < 40 && app.replay_state() == ReplayState::Jumping; ++i) app.update_simulation(0.05f);
        check(app.replay_state() != ReplayState::Jumping && app.replay_value(ReplayValue::Turn) >= 200 && app.replay_value(ReplayValue::Turn) < 220, "...and shows the match there (turn " + std::to_string(app.replay_value(ReplayValue::Turn)) + ")");
        const ViewportCamera& view = app.renderer().camera();
        const double mid_x = static_cast<double>(view.x) + zoom::visible_exact(view.viewport_w, view.zoom) / 2.0;
        const double mid_y = static_cast<double>(view.y) + zoom::visible_exact(view.viewport_h, view.zoom) / 2.0;
        check(std::fabs(mid_x - (shown.play.tile_x * 32.0 + 16.0)) < 40.0 && std::fabs(mid_y - (shown.play.tile_y * 32.0 + 16.0)) < 40.0, "...with the view on the place of the moment (" + std::to_string(mid_x) + ", " + std::to_string(mid_y) + " for the tile " + std::to_string(shown.play.tile_x) + ", " + std::to_string(shown.play.tile_y) + ")");
        check(app.can_watch_play() && app.replay_value(ReplayValue::Total) == static_cast<int>(shown.turns), "the viewer of the match just played can watch again, and the recording is the match that was played");
        for (int i = 0; i < 2000 && !app.sim().is_match_over(); ++i) app.update_simulation(0.05f);
        app.update_results(0.3f);
        const MatchReport& end = card.report();
        check(app.sim().is_match_over() && card.is_open() && card.new_look() && end.ok && end.turns == live.turns && end.mvp.present == live.mvp.present && end.mvp.seat == live.mvp.seat && end.first_blood.present == live.first_blood.present &&
                  end.play.present == live.play.present && end.damage_done == live.damage_done && card.watch_available() && card.link_rect().w > 0,
              "the viewer's end page is the match's own Podium again, from the report that was kept: the honors, the Play of the game, Match stats");
        click(card, card.link_rect());
        check(card.page() == ScorecardModal::Page::Stats, "...its second page opens");
        app.watch_play(200);                                                            // (what its Watch does, with the play that this match has)
        check(app.replay_mode() && !card.is_open() && app.replay_value(ReplayValue::JumpTarget) == 200, "...and Watch jumps to the moment again (turn 200)");
        for (int i = 0; i < 40 && app.replay_state() == ReplayState::Jumping; ++i) app.update_simulation(0.05f);
        check(app.replay_state() != ReplayState::Jumping && app.replay_value(ReplayValue::Turn) >= 200 && app.replay_value(ReplayValue::Turn) < 220, "...and shows the match there (turn " + std::to_string(app.replay_value(ReplayValue::Turn)) + ")");
        app.shutdown();
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
    test_switch(arc, art);
    test_podium(arc, art);
    test_counted(arc, art);
    test_waiting(arc, art);
    test_187(arc, art);
    test_reflow(arc, art);
    test_buttons(arc, art);
    test_room(arc, art);
    test_pixels(arc, art);
    test_app();
}
