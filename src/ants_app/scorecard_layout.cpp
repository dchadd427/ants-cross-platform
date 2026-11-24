// The results screen's layout in units (scorecard_layout.hpp): plain numbers from the size of the page, the rows and the headline.
#include "ants_app/scorecard_layout.hpp"

#include <algorithm>

namespace ants::app::scorecard_layout {

namespace {

constexpr int32_t kTop = 44;                   // where the content starts under the banner
constexpr int32_t kHeadingsH = 34;             // the column headings' row
constexpr int32_t kLabelH = 20;                // "Winner!"
constexpr int32_t kOthersLabelH = 24;          // "other players..."
constexpr int32_t kWellGap = 14;               // under a well
constexpr int32_t kLeaveTopCard = 61;          // the big button's top, counted up from the bottom: 7 of shadow room, 14 of clay and the button's 40
constexpr int32_t kLeaveBigH = 40;
constexpr int32_t kRowHeights[] = {44, 40, 36, 32, 28};
constexpr int32_t kCardHeights[] = {62, 54, 48};

int32_t headline_height(int32_t lines) { return lines > 0 ? 10 + 16 * lines : 0; }

/// Where a card's name, its numbers and the little labels under them stand (baselines, from the card's top): the lower the card, the closer they stand
struct CardBases {
    int32_t name, number, label;
};
constexpr CardBases card_bases(int32_t card_h) { return card_h >= 62 ? CardBases{22, 46, 57} : card_h >= 54 ? CardBases{20, 40, 50} : CardBases{18, 35, 44}; }

int32_t table_fixed(int32_t rh, int32_t others) {
    return kTop + kHeadingsH + kLabelH + (rh + kWellGap) + (others > 0 ? kOthersLabelH + others * rh + kWellGap : 0) + 14;
}

int32_t card_fixed(int32_t ch, int32_t others) {
    return kTop + kLabelH + (ch + kWellGap) + (others > 0 ? kOthersLabelH + others * ch + kWellGap : 0) + 8 + kLeaveTopCard;
}

/// The size of "Your Score" for `room` units of height to spare and a column of `cw`: the largest big one that fits, else Small
ScoreSize pick_score(int32_t free_h, int32_t cw, int32_t& cap) {
    cap = 0;
    if (free_h < 44) return ScoreSize::Small;
    const int32_t most = std::min({38, free_h - 16, cw / 9});
    if (most >= 38) { cap = 38; return ScoreSize::Big38; }
    if (most >= 33) { cap = 33; return ScoreSize::Big33; }
    if (most >= 28) { cap = 28; return ScoreSize::Big28; }
    return ScoreSize::Small;
}

}  // namespace

Layout compute(const Input& in) {
    Layout l;
    l.w = in.w;
    l.h = in.h;
    const int32_t rows = static_cast<int32_t>(std::min<size_t>(in.rows, kMaxRows));
    if (rows < 1 || in.w <= 0 || in.h <= 0) return l;
    l.ok = true;
    l.row_count = static_cast<size_t>(rows);
    const int32_t others = rows - 1;
    const int32_t nc = in.columns >= 4 ? 4 : 3;
    const int32_t hl_h = headline_height(in.headline_lines);

    // which layout: cards when the width asks for them and the height holds them (at the lowest card that fits)
    int32_t card_h = 0;
    if (in.w < kNarrowBelow) {
        for (const int32_t ch : kCardHeights) {
            if (card_fixed(ch, others) + 26 + hl_h <= in.h) { card_h = ch; break; }
        }
    }
    l.narrow = card_h > 0;
    const int32_t margin = in.w < kNarrowBelow ? 14 : 22;
    const int32_t cw = std::min(in.w - 2 * margin, kMaxContent);
    const int32_t x0 = (in.w - cw) / 2;
    l.content = LayoutRect{x0, 0, cw, in.h};
    l.title_cx = in.w / 2;
    l.title_base = 31;
    l.headline_cx = in.w / 2;
    l.headline_w = cw;
    l.label_x = x0 + 3;
    l.unit_text = l.narrow ? 16 : 15;
    l.waiting = LayoutRect{x0, in.h / 2 - 20, cw, 40};

    int32_t y = kTop;
    if (in.headline_lines > 0) {
        l.headline_base = y + 13;
        y += hl_h;
    }

    int32_t rh = 0;
    if (l.narrow) {
        rh = card_h;
        const int32_t free_h = in.h - card_fixed(rh, others) - hl_h;
        int32_t cap = 0;
        l.score = pick_score(free_h, cw, cap);
        l.score_centered = true;
        l.score_x = in.w / 2;
        if (l.score != ScoreSize::Small) {
            y += std::max(0, std::min(24, (free_h - cap - 16) / 6));
            l.score_base = y + cap + 4;
            y += cap + 16;
        } else {
            l.score_base = y + 19;
            y += 26;
        }
    } else {
        rh = kRowHeights[std::size(kRowHeights) - 1];
        for (const int32_t candidate : kRowHeights) {
            if (table_fixed(candidate, others) + hl_h <= in.h) { rh = candidate; break; }
        }
        const int32_t free_h = in.h - table_fixed(rh, others) - hl_h;
        int32_t cap = 0;
        l.score = pick_score(free_h, cw, cap);
        if (l.score != ScoreSize::Small) {
            y += std::max(0, std::min(60, (free_h - cap - 16) / 2));
            l.score_x = x0 + 10;
            l.score_base = y + cap + 4;
            y += cap + 16;
        } else {
            l.score_centered = true;
            l.score_x = x0 + 62;
            l.score_base = y + 19;
        }
        l.headings = true;
        l.heading_base = y + 16;
        l.sub_base = y + 28;
        y += kHeadingsH;
    }

    // the wells and their rows
    l.winner_base = y + 15;
    y += kLabelH;
    l.winner_well = LayoutRect{x0 + 4, y + 4, cw - 10, rh + 4};
    const int32_t first_top = y + 6;
    y += rh + kWellGap;
    int32_t others_top = 0;
    if (others > 0) {
        l.others_base = y + 17;
        y += kOthersLabelH;
        l.others_well = LayoutRect{x0 + 4, y + 4, cw - 10, others * rh + 4};
        others_top = y + 6;
    }
    l.divider_x = x0 + 12;
    l.divider_w = cw - 26;

    const int32_t pitch = in.w >= kNarrowBelow ? 78 : std::min(78, std::max(52, (cw - 152) / nc));
    const int32_t card_w = nc > 0 ? (cw - 88) / nc : 0;
    for (int32_t i = 0; i < rows; ++i) {
        RowSlots& r = l.row[static_cast<size_t>(i)];
        r.top = i == 0 ? first_top : others_top + (i - 1) * rh;
        r.height = rh;
        r.ants_left = x0 + 14;
        r.name_x = x0 + 76;
        if (l.narrow) {
            r.name_w = cw - 92;
            const CardBases bases = card_bases(rh);
            r.name_base = r.top + bases.name;
            r.number_base = r.top + bases.number;
            r.label_base = r.top + bases.label;
            for (int32_t c = 0; c < nc; ++c) r.number_x[static_cast<size_t>(c)] = x0 + 74 + (card_w * (2 * c + 1)) / 2;
            r.number_w = card_w - 4;
        } else {
            r.name_base = r.top + rh / 2 + 5;
            r.number_base = r.name_base;
            r.number_w = std::min(50, pitch - 8);
            for (int32_t c = 0; c < nc; ++c) r.number_x[static_cast<size_t>(c)] = x0 + cw - 16 - (nc - 1 - c) * pitch;
            r.name_w = std::max(0, r.number_x[0] - r.number_w - 10 - r.name_x);
        }
    }
    for (int32_t c = 0; c < nc; ++c) l.column_right[static_cast<size_t>(c)] = x0 + cw - 16 - (nc - 1 - c) * pitch;

    if (l.narrow) {
        l.leave_big = true;
        l.leave = LayoutRect{x0 + 2, in.h - kLeaveTopCard, cw - 6, kLeaveBigH};
        l.leave_word_base = l.leave.y + 26;
    } else {
        l.leave = LayoutRect{x0 + cw - kLeaveW + 1, 13, kLeaveFaceW, kLeaveFaceH};
    }
    return l;
}

}  // namespace ants::app::scorecard_layout
