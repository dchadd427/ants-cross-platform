// The results screen's layout in units (scorecard_layout.hpp): plain numbers from the size of the page, the sides and what was counted.
#include "ants_app/scorecard_layout.hpp"

#include <algorithm>

namespace ants::app::scorecard_layout {

namespace {

constexpr int32_t kLeaveBigTop = 61;           // the big button's top, counted up from the bottom: 7 of shadow room, 14 of clay and the button's 40

/// The box of a well's bezel (the ground with 4 more all round: wells that are 8 apart touch, and their soft shadows, 2 more on the right and below, overlap)
LayoutRect picture_of_well(const LayoutRect& g) { return LayoutRect{g.x - 4, g.y - 4, g.w + 8, g.h + 8}; }

void add(std::vector<Part>& parts, const char* name, const LayoutRect& r) {
    if (r.w > 0 && r.h > 0) parts.push_back(Part{name, r});
}

/// The buttons that every page has: Leave Game (the corner's button, or the big one at the bottom of a narrow page) or, after a lobby room's match, its two ("Back to the room" where Leave Game stands, and
/// "Back to Ants & Uncles" beside it: on a narrow page one above the other), and, when there is a second page, the link to it (a face as wide as `link_w`; a narrow page's is as wide as its column)
struct Header {
    LayoutRect link;
    LayoutRect leave;
    LayoutRect hub;
    bool choice{false};
    bool leave_big{false};
    int32_t leave_word_base{0};
    int32_t hub_word_base{0};
};

constexpr int32_t kStackPitch = kBigFaceH + 12;        // two big buttons one above the other: the face, its cast shadow and 8 of air
constexpr int32_t kButtonGap = 11;                     // two buttons side by side: the cast shadow (3) and 8 of air

Header header(int32_t h, bool narrow, int32_t x0, int32_t cw, int32_t link_w, bool choice) {
    Header out;
    out.choice = choice;
    if (narrow) {
        out.leave_big = true;
        const LayoutRect bottom{x0 + 2, h - kLeaveBigTop, cw - 6, kBigFaceH};
        if (choice) {
            out.hub = bottom;
            out.leave = LayoutRect{bottom.x, bottom.y - kStackPitch, bottom.w, bottom.h};
            out.hub_word_base = out.hub.y + 26;
        } else {
            out.leave = bottom;
        }
        out.leave_word_base = out.leave.y + 26;
        if (link_w > 0) out.link = LayoutRect{x0 + 2, 13, cw - 6, kSmallFaceH};
    } else {
        if (choice) {
            out.hub = LayoutRect{x0 + cw - kHubFaceW - 2, 13, kHubFaceW, kSmallFaceH};
            out.leave = LayoutRect{out.hub.x - kButtonGap - kRoomFaceW, 13, kRoomFaceW, kSmallFaceH};
        } else {
            out.leave = LayoutRect{x0 + cw - kLeaveW + 1, 13, kLeaveFaceW, kLeaveFaceH};
        }
        if (link_w > 0) out.link = LayoutRect{x0 + 2, 13, link_w, kSmallFaceH};
    }
    return out;
}

/// A button's picture is its face with the cast shadow 3 across and 4 down
LayoutRect with_shadow(const LayoutRect& face) { return LayoutRect{face.x, face.y, face.w + 3, face.h + 4}; }

void put_header(const Header& hd, std::vector<Part>& parts) {
    add(parts, hd.choice ? "Back to the room" : "Leave Game", with_shadow(hd.leave));
    if (hd.choice) add(parts, "Back to Ants & Uncles", with_shadow(hd.hub));
    if (hd.link.w > 0) add(parts, "the link to the other page", with_shadow(hd.link));
}

/// The podium with everything from the title down moved `dy` lower (a wide page that has room to spare is centered in its height)
Podium podium_at(const PodiumInput& in, int32_t dy, int32_t& bottom) {
    Podium p;
    p.w = in.w;
    p.h = in.h;
    const int32_t rows = static_cast<int32_t>(std::min<size_t>(std::max<size_t>(in.rows, 1), kMaxRows));
    const int32_t others = rows - 1;
    p.ok = true;
    p.narrow = in.w < kNarrowBelow;
    p.report = in.report;
    const int32_t margin = p.narrow ? 14 : 22;
    const int32_t cw = std::min(in.w - 2 * margin, kMaxContent);
    const int32_t x0 = (in.w - cw) / 2;
    p.content = LayoutRect{x0, 0, cw, in.h};
    const Header hd = header(in.h, p.narrow, x0, cw, in.report ? kAllStatsFaceW : 0, in.choice);
    p.link_shown = hd.link.w > 0;
    p.link = hd.link;
    p.leave = hd.leave;
    p.hub = hd.hub;
    p.choice = hd.choice;
    p.leave_big = hd.leave_big;
    p.leave_word_base = hd.leave_word_base;
    p.hub_word_base = hd.hub_word_base;
    put_header(hd, p.parts);

    // the title and its sentence
    int32_t wx = x0 + 4, wy = 0, ww = 0, wh = 0;
    if (p.narrow) {
        p.title_big = false;
        p.title_centered = true;
        p.title_x = in.w / 2;
        p.title_base = 68 + dy;
        p.sentence_x = in.w / 2;
        p.sentence_base = p.title_base + 20;
        p.sentence_w = cw;
        wy = 96;
        ww = cw - 10;
        wh = 124;
    } else {
        p.title_big = true;
        p.title_x = x0 + 8;
        p.title_base = 70 + dy;
        p.sentence_x = x0 + 22 + in.title_w;
        p.sentence_base = 68 + dy;
        p.sentence_w = std::max(0, x0 + cw - 6 - p.sentence_x);
        wy = 85;
        ww = others > 0 ? 270 : cw - 10;
        wh = 148;
    }
    wy += dy;
    const int32_t cap = p.title_big ? 26 : 22;
    add(p.parts, "the title", p.title_centered ? LayoutRect{p.title_x - in.title_w / 2, p.title_base - cap * 11 / 10, in.title_w, cap * 11 / 10 + 5}
                                               : LayoutRect{p.title_x, p.title_base - cap * 11 / 10, in.title_w, cap * 11 / 10 + 5});
    add(p.parts, "the sentence", LayoutRect{p.sentence_x - (p.narrow ? p.sentence_w / 2 : 0), p.sentence_base - 11, p.sentence_w, 14});

    // the winner's well: its label, the ants at double size, the name, the score, the tiles
    p.winner_well = LayoutRect{wx, wy, ww, wh};
    add(p.parts, "the winner's well", picture_of_well(p.winner_well));
    p.winner_label_x = wx + 10;
    p.winner_label_base = wy + 17;
    p.ants_x = wx + 10;
    p.ants_y = wy + 24;
    const int32_t aw = std::max(1, in.winner_ants) * (kAntW * 2 + 2);
    p.name_x = wx + 16 + aw;
    p.name_base = wy + 48;
    p.name_w = std::max(0, wx + ww - 8 - p.name_x);
    p.score_base = wy + 86;
    p.tile_count = static_cast<size_t>((in.is_187 ? 2 : 3) + (in.report ? 1 : 0));
    p.tile_base = wy + (p.narrow ? 108 : 116);
    p.tile_label_base = p.tile_base + 11;
    const double tw = static_cast<double>(ww - (16 + aw) - 8) / static_cast<double>(p.tile_count);
    for (size_t i = 0; i < p.tile_count; ++i) p.tile[i].x = p.name_x + static_cast<int32_t>(tw * (static_cast<double>(i) + 0.5)) - 4;

    // the others: a well of ranked rows, a bar of the score in each
    int32_t others_bottom = 0;
    p.other_count = static_cast<size_t>(others);
    if (others > 0) {
        const int32_t ox = p.narrow ? x0 + 4 : x0 + 292;
        const int32_t oy = (p.narrow ? wy + wh + 30 : 105 + dy);
        const int32_t ow = p.narrow ? cw - 10 : cw - 298;
        const int32_t rh = (!p.narrow && others > 2) ? 42 : 44;
        p.others_label_x = ox - 1;
        p.others_label_base = oy - 9;
        p.others_well = LayoutRect{ox, oy, ow, others * rh + 4};
        add(p.parts, "the others' well", picture_of_well(p.others_well));
        for (int32_t i = 0; i < others; ++i) {
            OtherRow& r = p.other[static_cast<size_t>(i)];
            const int32_t t = oy + 2 + i * rh;
            r.box = LayoutRect{ox, t, ow, rh};
            r.rank_x = ox + 9;
            r.rank_base = t + 26;
            r.head_x = ox + 20;
            r.head_y = t + 2;
            r.name_x = ox + 26 + std::max(1, in.other_ants[static_cast<size_t>(i)]) * (kAntW + 2);
            r.name_base = t + 17;
            r.name_right = ox + ow - 18;
            r.score_right = ox + ow - 10;
            r.score_base = t + 18;
            r.bar = LayoutRect{r.name_x, t + 22, std::max(1, ox + ow - 10 - r.name_x), 3};
            r.small_base = t + 36;
        }
        others_bottom = oy + others * rh + 4;
    }

    // the bottom: the Play of the game and the three honors (when the match was counted)
    int32_t y = p.narrow ? (others > 0 ? others_bottom + 12 : wy + wh + 16) : std::max(wy + wh, others_bottom) + 16;
    bottom = std::max(wy + wh, others_bottom);
    if (in.report) {
        if (p.narrow) {
            int32_t play_h = 74, honor_h = 94;
            const int32_t room = hd.leave.y - 8;
            while (y + (in.play ? play_h + 14 : 0) + honor_h > room && (honor_h > 66 || play_h > 60)) {          // (a short page: the cards are lower)
                if (honor_h > 66) honor_h -= 4;
                else if (play_h > 60) play_h -= 2;
            }
            if (in.play && y + play_h <= room) {                                    // (a page too short for the play has none: the honors, if they fit, are what it has)
                p.play = LayoutRect{x0 + 4, y, cw - 10, play_h};
                const int32_t th = play_h - 10;
                p.play_picture = LayoutRect{p.play.x + 5, p.play.y + 5, th * 3 / 2, th};
                p.play_text_x = p.play_picture.right() + 13;
                p.play_label_base = y + 13;
                p.play_line_base = y + 28;
                p.watch = LayoutRect{p.play.right() - kWatchW - 4, p.play.bottom() - kWatchH - 3, kWatchFaceW, kWatchFaceH};
                add(p.parts, "the play of the game", picture_of_well(p.play));
                add(p.parts, "Watch", LayoutRect{p.watch.x, p.watch.y, p.watch.w + 3, p.watch.h + 4});
                bottom = p.play.bottom();
                y += play_h + 14;
            }
            if (y + honor_h <= room) {
                p.honors = true;
                const int32_t hw = (cw - 26) / 3;
                for (int32_t i = 0; i < 3; ++i) p.honor[static_cast<size_t>(i)] = LayoutRect{x0 + 4 + i * (hw + 8), y, hw, honor_h};
                p.honor_label_base = 15;
                p.honor_ant_y = 19 + std::max(0, (honor_h - 84) / 3);
                p.honor_name_base = honor_h - 17;
                p.honor_detail_base = honor_h - 6;
                p.honor_ant_h = std::min(kAntH, p.honor_name_base - 6 - p.honor_ant_y);
                bottom = y + honor_h;
            }
        } else {
            const int32_t hh = std::min(in.h - 16 - y, 96);
            const bool cramped = in.play && (cw - 340) / 3 < 84;                    // (no room for the play and three honors side by side: the play takes the width, and the honors a row of their own if there is one)
            const auto honors_at = [&](int32_t hx, int32_t hw, int32_t top, int32_t height) {
                p.honors = true;
                for (int32_t i = 0; i < 3; ++i) p.honor[static_cast<size_t>(i)] = LayoutRect{hx + i * (hw + 8), top, hw, height};
                p.honor_label_base = 15;
                p.honor_ant_y = 19 + std::max(0, (height - 84) / 3);
                p.honor_name_base = height - 17;
                p.honor_detail_base = height - 6;
                p.honor_ant_h = std::min(kAntH, p.honor_name_base - 6 - p.honor_ant_y);
                bottom = top + height;
            };
            if (hh >= 70) {
                if (in.play) {
                    p.play = LayoutRect{x0 + 4, y, cramped ? cw - 10 : 296, hh};
                    const int32_t th = hh - 12;
                    p.play_picture = LayoutRect{p.play.x + 5, y + (hh - th) / 2, th * 3 / 2, th};
                    p.play_text_x = p.play_picture.right() + 13;
                    p.play_label_base = y + 16;
                    p.play_line_base = y + 32;
                    p.watch = LayoutRect{p.play_text_x, y + hh - 29, kWatchFaceW, kWatchFaceH};
                    add(p.parts, "the play of the game", picture_of_well(p.play));
                    add(p.parts, "Watch", LayoutRect{p.watch.x, p.watch.y, p.watch.w + 3, p.watch.h + 4});
                    bottom = y + hh;
                }
                if (!in.play) {
                    honors_at(x0 + 4, (cw - 10 - 16) / 3, y, hh);
                } else if (!cramped) {
                    honors_at(x0 + 318, (cw - 318 - 6 - 16) / 3, y, hh);
                } else {
                    const int32_t y2 = y + hh + 14;
                    const int32_t hh2 = std::min(in.h - 16 - y2, 96);
                    if (hh2 >= 70) honors_at(x0 + 4, (cw - 10 - 16) / 3, y2, hh2);
                }
            }
        }
        if (p.honors) {
            for (size_t i = 0; i < 3; ++i) add(p.parts, i == 0 ? "the first honor" : i == 1 ? "the second honor" : "the third honor", picture_of_well(p.honor[i]));
        }
    }
    return p;
}

}  // namespace

Podium compute_podium(const PodiumInput& in) {
    if (in.w <= 0 || in.h <= 0) return Podium{};
    int32_t bottom = 0;
    const Podium first = podium_at(in, 0, bottom);
    if (first.narrow) return first;                                       // (a narrow page is made from the top, and its Leave Game stands at the bottom)
    const int32_t spare = in.h - (bottom + 10) - 16;                      // (the bottom well's picture and a margin of its own: the page is centered in its height when it has room)
    const int32_t dy = std::max(0, std::min(60, spare / 2));
    if (dy == 0) return first;
    int32_t moved = 0;
    return podium_at(in, dy, moved);
}

Waiting compute_waiting(int32_t w, int32_t h, bool choice) {
    Waiting out;
    if (w <= 0 || h <= 0) return out;
    out.ok = true;
    out.narrow = w < kNarrowBelow;
    const int32_t margin = out.narrow ? 14 : 22;
    const int32_t cw = std::min(w - 2 * margin, kMaxContent);
    const int32_t x0 = (w - cw) / 2;
    out.title_big = !out.narrow;
    const int32_t cy = h / 2 - 10;
    out.title_cx = w / 2;
    out.title_base = cy;
    out.sentence_base = cy + 30;
    out.sentence_w = cw;
    const Header hd = header(h, out.narrow, x0, cw, 0, choice);
    out.leave = hd.leave;
    out.hub = hd.hub;
    out.choice = hd.choice;
    out.leave_big = hd.leave_big;
    out.leave_word_base = hd.leave_word_base;
    out.hub_word_base = hd.hub_word_base;
    put_header(hd, out.parts);
    const int32_t cap = out.title_big ? 36 : 26;
    add(out.parts, "the title", LayoutRect{w / 2 - 6 * cap, cy - cap * 11 / 10, 12 * cap, cap * 11 / 10 + 5});
    add(out.parts, "the sentence", LayoutRect{x0, cy + 30 - 13, cw, 17});
    return out;
}

StatsPage compute_stats(const StatsInput& in) {
    StatsPage s;
    if (in.w <= 0 || in.h <= 0) return s;
    const int32_t n = static_cast<int32_t>(std::min<size_t>(std::max<size_t>(in.seats, 1), 4));
    s.ok = true;
    s.narrow = in.w < kNarrowBelow;
    s.seat_count = static_cast<size_t>(n);
    const int32_t margin = s.narrow ? 14 : 22;
    const int32_t cw = std::min(in.w - 2 * margin, kMaxContent);
    const int32_t x0 = (in.w - cw) / 2;
    s.content = LayoutRect{x0, 0, cw, in.h};
    const Header hd = header(in.h, s.narrow, x0, cw, kBackFaceW, in.choice);
    s.link = hd.link;
    s.leave = hd.leave;
    s.hub = hd.hub;
    s.choice = hd.choice;
    s.leave_big = hd.leave_big;
    s.leave_word_base = hd.leave_word_base;
    s.hub_word_base = hd.hub_word_base;
    put_header(hd, s.parts);
    // the numbers: Damage (done, taken), Food (trips, points), Special ants (bombs set, fires lit, raids, loot); the 187 page has no food
    std::vector<int32_t> counts = in.is_187 ? std::vector<int32_t>{2, 4} : std::vector<int32_t>{2, 2, 4};
    s.groups = counts.size();
    int32_t nc = 0;
    for (const int32_t c : counts) nc += c;
    s.columns = static_cast<size_t>(nc);
    int32_t y = 44;
    if (s.narrow) {
        const int32_t per = 4;
        const int32_t card = 46 + 27 * ((nc + per - 1) / per) - 6;
        s.well = LayoutRect{x0 + 4, y + 4, cw - 10, n * card + 4};
        const int32_t cwid = (cw - 24) / per;
        s.number_w = cwid - 4;
        for (int32_t i = 0; i < nc; ++i) s.column_right[static_cast<size_t>(i)] = x0 + 12 + cwid * (i % per) + cwid / 2;
        for (int32_t q = 0; q < n; ++q) {
            StatsPage::Seat& seat = s.seat[static_cast<size_t>(q)];
            const int32_t top = y + 6 + q * card;
            seat.box = LayoutRect{x0 + 4, top, cw - 10, card};
            seat.ant_x = x0 + 16;
            seat.ant_y = top + 2;
            seat.name_x = x0 + 50;
            seat.name_base = top + 24;
            seat.name_w = cw - 70;
            for (int32_t i = 0; i < nc; ++i) {
                seat.number_x[static_cast<size_t>(i)] = s.column_right[static_cast<size_t>(i)];
                seat.number_base[static_cast<size_t>(i)] = top + 50 + 27 * (i / per);
                seat.label_base[static_cast<size_t>(i)] = seat.number_base[static_cast<size_t>(i)] + 11;
            }
        }
        add(s.parts, "the well", picture_of_well(s.well));
        return s;
    }
    // the columns' pitch is 54 units, less on a page with less room: the first group's line starts after the title of the page (the title ends 131 units right of the column's edge)
    const int32_t gap = 12, rh = 44;
    const int32_t pitch = std::max(38, std::min(54, (cw - 133 - (static_cast<int32_t>(counts.size()) - 1) * gap) / nc));
    int32_t x = x0 + cw - 16;
    int32_t col = nc;
    for (int32_t g = static_cast<int32_t>(counts.size()) - 1; g >= 0; --g) {
        const int32_t right = x;
        for (int32_t k = 0; k < counts[static_cast<size_t>(g)]; ++k) {
            s.column_right[static_cast<size_t>(--col)] = x;
            x -= pitch;
        }
        s.group_line[static_cast<size_t>(g)] = LayoutRect{x + 14, 0, right - (x + 14), 1};
        s.group_cx[static_cast<size_t>(g)] = (x + 14 + right) / 2;
        x -= gap;
    }
    s.number_w = pitch - 6;
    y += std::max(0, std::min(34, (in.h - y - 54 - n * rh - 30) / 2));
    s.title_cx = x0 + 62;
    s.title_base = y + 21;
    s.group_base = y + 13;
    s.sub_base = y + 30;
    for (size_t g = 0; g < s.groups; ++g) s.group_line[g].y = y + 18;
    y += 34;
    s.well = LayoutRect{x0 + 4, y + 4, cw - 10, n * rh + 4};
    add(s.parts, "the well", picture_of_well(s.well));
    for (int32_t q = 0; q < n; ++q) {
        StatsPage::Seat& seat = s.seat[static_cast<size_t>(q)];
        const int32_t top = y + 6 + q * rh;
        seat.box = LayoutRect{x0 + 4, top, cw - 10, rh};
        seat.ant_x = x0 + 18;
        seat.ant_y = top + 1;
        seat.name_x = x0 + 54;
        seat.name_base = top + rh / 2 + 6;
        seat.name_w = std::max(0, s.column_right[0] - s.number_w - 8 - seat.name_x);
        for (int32_t i = 0; i < nc; ++i) {
            seat.number_x[static_cast<size_t>(i)] = s.column_right[static_cast<size_t>(i)];
            seat.number_base[static_cast<size_t>(i)] = seat.name_base;
        }
    }
    add(s.parts, "the title", LayoutRect{x0 + 62 - 40, s.title_base - 18, 80, 24});
    return s;
}

}  // namespace ants::app::scorecard_layout
