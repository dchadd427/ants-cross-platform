// The wide match screen, part 1: the anchoring model of the frame, its cuts, the composed picture and the renderer drawing the spans (groups A - D).
// (See test_wide_hud.cpp for what the whole program checks.)
#include "wide_hud_fixtures.hpp"

namespace {

/// The approved mock-up of 2026-10-02 (a small tool that composes the frame from the original's pieces), written out here independently of the production header: the 14 pieces in the order the original draws them, each moved by the
/// right / bottom anchors and drawn with ONE of its columns or rows repeated dW or dH times (the line `col` / `row`)
struct RefPiece {
    const char* name;
    int32_t x, y;
    bool right, bottom;
    std::array<int32_t, 3> cols;     // the repeated columns (-1: none); dW is shared between them in thirds, the leftmost taking the remainder
    int32_t row;                     // the repeated row (-1: none)
};

constexpr RefPiece kReference[14] = {
    {"x0y22.bmp", 0, 22, false, false, {-1, -1, -1}, 300},        // the left strip: taller below its horizontal rule
    {"x458y22.bmp", 458, 22, true, false, {-1, -1, -1}, -1},      // the top of the right panel
    {"x458y35.bmp", 458, 35, true, false, {-1, -1, -1}, 322},     // the strip between the map and the panel: taller at canvas row 357
    {"x480y126.bmp", 480, 126, true, false, {-1, -1, -1}, -1},
    {"x480y266.bmp", 480, 266, true, false, {-1, -1, -1}, -1},
    {"x480y400.bmp", 480, 400, true, true, {-1, -1, -1}, -1},
    {"x480y466.bmp", 480, 436, true, true, {-1, -1, -1}, -1},
    {"x521y254.bmp", 621, 254, true, false, {-1, -1, -1}, 103},   // the right edge strip: taller at canvas row 357
    {"x599y35.bmp", 599, 35, true, false, {-1, -1, -1}, -1},
    {"wchat.bmp", 479, 298, true, false, {-1, -1, -1}, 59},       // the chat log's box: taller at canvas row 357
    {"wtype.bmp", 479, 423, true, true, {-1, -1, -1}, -1},
    {"wstatus.bmp", 479, 253, true, false, {-1, -1, -1}, -1},
    {"x0y0.bmp", 0, 0, false, false, {140, -1, -1}, -1},          // the top bar: wider at its plain green
    {"x17y461.bmp", 17, 461, false, true, {15, 188, 346}, -1},    // the bottom strip: wider at three plain cuts (the score boxes are spread), down by dH
};

/// The mock-up's rule (the reference tool, draw_multi): the extra width dW is shared between the cuts in thirds (a third each for three cuts; the leftmost takes the remainder), and
/// every cut column is repeated by its share
std::vector<int32_t> reference_shares(int32_t dw, size_t cuts) {
    std::vector<int32_t> shares(cuts, cuts > 0 ? dw / static_cast<int32_t>(cuts) : 0);
    if (cuts > 0) shares[0] = dw - (static_cast<int32_t>(cuts) - 1) * (dw / static_cast<int32_t>(cuts));
    return shares;
}

/// The columns of a piece of `w` columns that is widened by dw at the cut columns `cuts` (ascending): (source column, destination column within the piece), the mock-up's draw_multi
std::vector<std::pair<int32_t, int32_t>> reference_columns(int32_t w, const std::vector<int32_t>& cuts, int32_t dw) {
    const std::vector<int32_t> shares = reference_shares(dw, cuts.size());
    std::vector<std::pair<int32_t, int32_t>> columns;
    int32_t shift = 0;
    for (int32_t sx = 0; sx < w; ++sx) {
        columns.emplace_back(sx, sx + shift);
        for (size_t i = 0; i < cuts.size(); ++i) {
            if (cuts[i] != sx) continue;
            for (int32_t k = 1; k <= shares[i]; ++k) columns.emplace_back(sx, sx + shift + k);
            shift += shares[i];
        }
    }
    return columns;
}

Pic compose_reference(const assets::AssetArchive& arc, int32_t width, int32_t height) {
    Pic pic(width, height);
    const int32_t dw = width - 640;
    const int32_t dh = height - 480;
    for (const RefPiece& p : kReference) {
        const assets::Sprite* sprite = arc.find_sprite(p.name);
        if (sprite == nullptr) continue;
        const int32_t ox = p.right ? dw : 0;
        const int32_t oy = p.bottom ? dh : 0;
        std::vector<int32_t> cuts;
        for (const int32_t c : p.cols) {
            if (c >= 0) cuts.push_back(c);
        }
        const int32_t eh = p.row >= 0 ? dh : 0;
        // the columns: a source column goes to its place, shifted by the shares of the cuts left of it, and a cut column is written 1 + its share times
        const std::vector<std::pair<int32_t, int32_t>> columns = reference_columns(static_cast<int32_t>(sprite->width), cuts, dw);
        for (int32_t y = 0; y < static_cast<int32_t>(sprite->height) + eh; ++y) {
            const int32_t sy = (eh > 0 && y > p.row) ? (y <= p.row + eh ? p.row : y - eh) : y;
            for (const auto& col : columns) {
                const uint8_t v = sprite->get_pixel(static_cast<uint32_t>(col.first), static_cast<uint32_t>(sy));
                if (v != assets::CHD_COLOR_KEY_INDEX) pic.put(p.x + ox + col.second, p.y + oy + y, v);
            }
        }
    }
    return pic;
}

int64_t differing_pixels(const Pic& a, const Pic& b) {
    if (a.w != b.w || a.h != b.h) return -1;
    int64_t n = 0;
    for (size_t i = 0; i < a.idx.size(); ++i) n += a.idx[i] != b.idx[i] ? 1 : 0;
    return n;
}

// =====================================================================================================================================================
// 1. The anchoring model
// =====================================================================================================================================================

/// The 14 pieces of `uishell` as the original places them (animation frames of Table 4: the part's offset and the sprite's size) and the bounding box of each in a picture of
/// 960 x 540 (dx = 320, dy = 60), written out by hand: the right panel is pinned to the right edge, the bottom parts go down by 60, the pieces that are stretched grow
struct PieceNumbers {
    const char* name;
    LayoutRect classic;
    LayoutRect wide;
};

constexpr PieceNumbers kPieces[14] = {
    {"x17y461.bmp", {17, 461, 623, 19}, {17, 521, 943, 19}},       // the bottom strip: down by 60, 320 wider
    {"x0y0.bmp", {0, 0, 640, 22}, {0, 0, 960, 22}},                // the top bar: 320 wider
    {"wstatus.bmp", {479, 253, 143, 14}, {799, 253, 143, 14}},     // the status box: right
    {"wtype.bmp", {479, 423, 143, 14}, {799, 483, 143, 14}},       // the chat input box: right and down
    {"wchat.bmp", {479, 298, 143, 103}, {799, 298, 143, 163}},     // the chat log's box: right, 60 taller
    {"x599y35.bmp", {599, 35, 41, 91}, {919, 35, 41, 91}},         // the minimap's bezel: right
    {"x521y254.bmp", {621, 254, 19, 182}, {941, 254, 19, 242}},    // the right edge strip: right, 60 taller
    {"x480y466.bmp", {480, 436, 160, 25}, {800, 496, 160, 25}},    // the panel's bottom: right and down
    {"x480y400.bmp", {480, 400, 141, 24}, {800, 460, 141, 24}},    // the ant relief under the chat log: right and down
    {"x480y266.bmp", {480, 266, 141, 33}, {800, 266, 141, 33}},    // the chat header: right
    {"x480y126.bmp", {480, 126, 160, 128}, {800, 126, 160, 128}},  // the status card: right
    {"x458y35.bmp", {458, 35, 22, 426}, {778, 35, 22, 486}},       // the strip between the map and the panel: right, 60 taller
    {"x458y22.bmp", {458, 22, 182, 13}, {778, 22, 182, 13}},       // the top of the panel: right
    {"x0y22.bmp", {0, 22, 17, 458}, {0, 22, 17, 518}},             // the left strip: 60 taller
};

/// The spans are disjoint and their areas add up to the box's: they tile it (every span lies inside the box)
bool tiles_without_gap(const ShellSpans& spans, const LayoutRect& box) {
    int64_t area = 0;
    for (size_t i = 0; i < spans.count; ++i) {
        const LayoutRect& d = spans.span[i].dst;
        if (d.x < box.x || d.y < box.y || d.right() > box.right() || d.bottom() > box.bottom()) return false;
        area += static_cast<int64_t>(d.w) * d.h;
        for (size_t j = i + 1; j < spans.count; ++j) {
            const LayoutRect& e = spans.span[j].dst;
            if (d.x < e.right() && e.x < d.right() && d.y < e.bottom() && e.y < d.bottom()) return false;
        }
    }
    return area == static_cast<int64_t>(box.w) * box.h;
}

}  // namespace

void test_anchoring(const assets::AssetArchive& arc) {
    group("anchors", "each of the 14 pieces of the frame at 960 x 540: where it is, how big, how it is stretched, and that its spans tile it");
    const auto* seq = arc.find_animation("uishell");
    check(seq != nullptr && !seq->subitems.empty() && seq->subitems[0].frames.size() == 14, "the animation uishell has 14 pieces");
    if (seq == nullptr || seq->subitems.empty() || seq->subitems[0].frames.size() != 14) return;
    const auto& frames = seq->subitems[0].frames;
    const ScreenLayout wide = ScreenLayout::with_size(960, 540);
    const ScreenLayout classic = ScreenLayout::classic();
    check(kShellRules.size() == 14, "the model has a rule for each of the 14");
    for (size_t k = 0; k < 14; ++k) {
        const assets::Sprite& sprite = arc.get_sprite(frames[k].sprite_index);
        const PieceNumbers& want = kPieces[k];
        const std::string at = "piece " + std::to_string(k) + " " + want.name + ": ";
        check(sprite.name == want.name, at + "the animation's name is " + sprite.name);
        const LayoutRect got{frames[k].dx, frames[k].dy, static_cast<int32_t>(sprite.width), static_cast<int32_t>(sprite.height)};
        check_rect(got, want.classic, at + "the original's place and size (the archive)");
        const ShellRule* rule = shell_rule(sprite.name);
        check(rule != nullptr && std::string(kShellRules[k].sprite) == want.name && rule == &kShellRules[k], at + "the model's rule is in the animation's order");
        // the original's picture: one span, the whole piece, where the original puts it
        const ShellSpans plain = shell_spans(rule, got.x, got.y, got.w, got.h, classic);
        check(plain.count == 1 && plain.span[0].whole && plain.span[0].dst == got && plain.span[0].src == (LayoutRect{0, 0, got.w, got.h}), at + "the classic picture draws it whole at its place");
        // 960 x 540
        const ShellSpans spans = shell_spans(rule, got.x, got.y, got.w, got.h, wide);
        check(spans.count >= 1 && tiles_without_gap(spans, want.wide), at + "its spans tile " + str(want.wide));
        // the source of every span lies inside the sprite
        bool inside = true;
        for (size_t i = 0; i < spans.count; ++i) {
            const LayoutRect& s = spans.span[i].src;
            inside = inside && s.x >= 0 && s.y >= 0 && s.right() <= got.w && s.bottom() <= got.h;
        }
        check(inside, at + "every span reads inside the sprite");
    }
    // how many of them grow: the top bar and the bottom strip in width, the left strip, the strip beside the map, the chat box and the right edge strip in height
    int widened = 0, heightened = 0, moved_right = 0, moved_down = 0;
    for (size_t k = 0; k < 14; ++k) {
        widened += kShellRules[k].cut_cols[0] >= 0 ? 1 : 0;
        heightened += kShellRules[k].cut_row >= 0 ? 1 : 0;
        moved_right += kShellRules[k].right ? 1 : 0;
        moved_down += kShellRules[k].bottom ? 1 : 0;
    }
    check(widened == 2 && heightened == 4, "two pieces are widened and four are made taller");
    check(moved_right == 11 && moved_down == 4, "eleven pieces are right anchored (the top bar, the bottom strip and the left strip are not), four go down");

    // other sizes follow the same rule: right anchored +dx, bottom anchored +dy, widened +dx, heightened +dy
    const struct { int32_t w, h; } sizes[] = {{1280, 720}, {1920, 1080}, {700, 500}, {960, 480}, {640, 540}, {960, 640}, {1200, 540}, {1260, 540}, {1920, 540}};          // (the last four are the shapes 3:2, 20:9, 21:9 and 32:9)
    for (const auto& sz : sizes) {
        const ScreenLayout l = ScreenLayout::with_size(sz.w, sz.h);
        const int32_t dx = l.dx(), dy = l.dy();
        bool ok = true;
        for (size_t k = 0; k < 14; ++k) {
            const LayoutRect c = kPieces[k].classic;
            const LayoutRect want{c.x + (kShellRules[k].right ? dx : 0), c.y + (kShellRules[k].bottom ? dy : 0), c.w + (kShellRules[k].cut_cols[0] >= 0 ? dx : 0),
                                  c.h + (kShellRules[k].cut_row >= 0 ? dy : 0)};
            const ShellSpans spans = shell_spans(&kShellRules[k], c.x, c.y, c.w, c.h, l);
            ok = ok && tiles_without_gap(spans, want);
        }
        check(ok, std::to_string(sz.w) + " x " + std::to_string(sz.h) + ": every piece's spans tile its box (right +" + std::to_string(dx) + ", down +" + std::to_string(dy) + ")");
    }
    // a name that is not a piece of the frame is one plain copy where the animation puts it
    const ShellSpans foreign = shell_spans(shell_rule("somewhere.bmp"), 10, 20, 30, 40, wide);
    check(shell_rule("somewhere.bmp") == nullptr && foreign.count == 1 && foreign.span[0].whole && foreign.span[0].dst == (LayoutRect{10, 20, 30, 40}), "a sprite that is not a piece is not moved");
}

namespace {

// =====================================================================================================================================================
// 2. The cuts: the repeated line lies in the plain part of the art
// =====================================================================================================================================================

/// The number of pixels in which two lines (rows when `rows`, columns otherwise) of a sprite differ
int32_t line_diff(const assets::Sprite& s, bool rows, int32_t a, int32_t b) {
    const int32_t n = rows ? static_cast<int32_t>(s.width) : static_cast<int32_t>(s.height);
    int32_t d = 0;
    for (int32_t i = 0; i < n; ++i) {
        const uint8_t pa = rows ? s.get_pixel(static_cast<uint32_t>(i), static_cast<uint32_t>(a)) : s.get_pixel(static_cast<uint32_t>(a), static_cast<uint32_t>(i));
        const uint8_t pb = rows ? s.get_pixel(static_cast<uint32_t>(i), static_cast<uint32_t>(b)) : s.get_pixel(static_cast<uint32_t>(b), static_cast<uint32_t>(i));
        d += pa != pb ? 1 : 0;
    }
    return d;
}

/// The run of identical lines around line c: the first and the last
std::pair<int32_t, int32_t> identical_run(const assets::Sprite& s, bool rows, int32_t c) {
    const int32_t n = rows ? static_cast<int32_t>(s.height) : static_cast<int32_t>(s.width);
    int32_t lo = c, hi = c;
    while (lo > 0 && line_diff(s, rows, lo - 1, lo) == 0) --lo;
    while (hi + 1 < n && line_diff(s, rows, hi, hi + 1) == 0) ++hi;
    return {lo, hi};
}

/// The distance of line c to the nearest decoration edge: a place where two neighbouring lines differ by at least `strong` pixels
int32_t distance_to_decoration(const assets::Sprite& s, bool rows, int32_t c, int32_t strong) {
    const int32_t n = rows ? static_cast<int32_t>(s.height) : static_cast<int32_t>(s.width);
    int32_t best = 1 << 20;
    for (int32_t i = 1; i < n; ++i) {
        if (line_diff(s, rows, i - 1, i) >= strong) best = std::min(best, std::min(std::abs(i - c), std::abs(i - 1 - c)));
    }
    return best;
}

}  // namespace

void test_cuts(const assets::AssetArchive& arc) {
    group("cuts", "every repeated line lies in the plain part of its piece: inside a run of identical lines, away from the clock box, the score boxes and the ant decorations");
    auto sprite = [&](const char* name) -> const assets::Sprite& { return *arc.find_sprite(name); };
    auto rule = [&](const char* name) -> const ShellRule& { return *shell_rule(name); };

    // --- the three runs of identical lines, measured on the art
    {   // the top bar: columns 138 - 143 (the clock box's right edge is column 129 - 132, the next decoration is far right)
        const assets::Sprite& s = sprite("x0y0.bmp");
        const int32_t c = rule("x0y0.bmp").cut_cols[0];
        const auto run = identical_run(s, false, c);
        check(col_cut_count(rule("x0y0.bmp")) == 1 && rule("x0y0.bmp").cut_cols[1] < 0 && rule("x0y0.bmp").cut_cols[2] < 0, "x0y0: one cut");
        check(run.first == 138 && run.second == 143, "x0y0: the run of identical columns around the cut is 138 - 143 (it is " + std::to_string(run.first) + " - " + std::to_string(run.second) + ")");
        check(c >= run.first + 1 && c <= run.second - 1, "x0y0: the cut column " + std::to_string(c) + " has identical columns on both sides of it");
        check(c > 132, "x0y0: the cut is right of the black clock box (its edge is column 129 - 132)");
        check(line_diff(s, false, c - 1, c) == 0 && line_diff(s, false, c, c + 1) == 0, "x0y0: the neighbours of the cut continue it exactly");
    }
    {   // the bottom strip: THREE cuts, one left of each score box: the plain band left of the first box (columns 13 - 17: 52.P correction), the plain run 186 - 190 right of the first box
        // and left of the second team's label, and the plain run 345 - 348 left of the third team's label; NOT the panel's own fill, columns 458 - 482
        const assets::Sprite& s = sprite("x17y461.bmp");
        const ShellRule& strip = rule("x17y461.bmp");
        check(col_cut_count(strip) == 3 && strip.cut_cols[0] == 15 && strip.cut_cols[1] == 188 && strip.cut_cols[2] == 346, "x17y461: three cuts, at the columns 15, 188 and 346");
        const struct { int32_t col, first, last; } runs[3] = {{15, 13, 17}, {188, 186, 190}, {346, 345, 348}};
        const ScreenLayout classic = ScreenLayout::classic();
        for (size_t i = 0; i < 3; ++i) {
            const int32_t c = strip.cut_cols[i];
            const std::string at = "x17y461 cut " + std::to_string(i) + " (column " + std::to_string(c) + "): ";
            const auto run = identical_run(s, false, c);
            check(run.first == runs[i].first && run.second == runs[i].last, at + "the run of identical columns around it is " + std::to_string(runs[i].first) + " - " + std::to_string(runs[i].last) + " (it is " + std::to_string(run.first) + " - " + std::to_string(run.second) + ")");
            check(c >= run.first + 1 && c <= run.second - 1, at + "it has identical columns on both sides");
            check(static_cast<int32_t>(s.height) == 19 && line_diff(s, false, c - 1, c) == 0 && line_diff(s, false, c, c + 1) == 0, at + "it is identical to its neighbours on all 19 rows");
            int32_t transparent = 0;
            for (int32_t y = 0; y < static_cast<int32_t>(s.height); ++y) transparent += s.get_pixel(static_cast<uint32_t>(c), static_cast<uint32_t>(y)) == assets::CHD_COLOR_KEY_INDEX ? 1 : 0;
            check(transparent == 0, at + "no row of it is transparent: the repeated column is solid art");
            // the cuts and the boxes: the recess of box b (54 wide) is at column box_left - 17; cut i is left of box i + 1 and right of the recess before it, so one cut sits in each gap
            const int32_t recess_left = classic.score_slot(i + 1).box_left - 17;
            check(c < recess_left - 1, at + "it is left of the recess of the box it moves (column " + std::to_string(recess_left) + ")");
            if (i > 0) {
                const int32_t before = classic.score_slot(i).box_left - 17;
                check(c > before + 54, at + "and right of the recess of the box before it (columns " + std::to_string(before) + " - " + std::to_string(before + 53) + "): the recesses are never cut");
            }
        }
        const auto fill_run = identical_run(s, false, 470);
        check(fill_run.first >= 457 && fill_run.second >= 482, "x17y461: columns 458 - 482 are an identical run too (the control: identical alone is not enough)");
        for (size_t i = 0; i < 3; ++i) check(!(strip.cut_cols[i] >= 458 && strip.cut_cols[i] <= 482), "x17y461: ... but that run is the right panel's own fill, and no cut is in it (a stretch there puts a flat block between the end ornament and the panel)");
        // the recesses themselves are long runs of identical columns too (the score boxes' flat inside): the control that a cut inside one would pass "identical" and has to fail the position test
        check(identical_run(s, false, 100).first <= 90 && identical_run(s, false, 100).second >= 136, "x17y461: the first recess is a run of identical columns (control: cutting there would be 'identical' and is excluded by the recess test)");
    }
    {   // the left strip: rows 294 - 314, below the horizontal rule (rows 260 - 266: it stays level with the chat header)
        const assets::Sprite& s = sprite("x0y22.bmp");
        const int32_t r = rule("x0y22.bmp").cut_row;
        const auto run = identical_run(s, true, r);
        check(run.first == 294 && run.second == 314, "x0y22: the run of identical rows around the cut is 294 - 314 (it is " + std::to_string(run.first) + " - " + std::to_string(run.second) + ")");
        check(r >= run.first + 1 && r <= run.second - 1, "x0y22: the cut row " + std::to_string(r) + " has identical rows on both sides");
        check(r > 266, "x0y22: the cut is below the horizontal rule (rows 260 - 266)");
        check(line_diff(s, true, 259, 260) >= 10 && line_diff(s, true, 266, 267) >= 1, "x0y22: the rule's rows are decoration (the control: the rows 260 - 266 are not plain)");
    }

    // --- the right panel: the strip beside the map, the chat box and the right edge strip are cut at ONE canvas row, y = 357
    const char* trio[3] = {"x458y35.bmp", "wchat.bmp", "x521y254.bmp"};
    for (const char* name : trio) {
        const assets::Sprite& s = sprite(name);
        const PieceNumbers* pn = nullptr;
        for (const PieceNumbers& p : kPieces) {
            if (std::string(p.name) == name) pn = &p;
        }
        const int32_t row = rule(name).cut_row;
        check(pn != nullptr && pn->classic.y + row == 357, std::string(name) + ": the cut is at canvas row 357 (the piece starts at " + std::to_string(pn ? pn->classic.y : -1) + ", row " + std::to_string(row) + ")");
        const int32_t above = line_diff(s, true, row - 1, row);
        const int32_t below = line_diff(s, true, row, row + 1);
        const std::string at = std::string(name) + ": ";
        check(above <= 5 && below <= 5, at + "the repeated row differs from its neighbours by " + std::to_string(above) + " and " + std::to_string(below) + " pixels (at most 5)");
        if (std::string(name) != "wchat.bmp") {
            const int32_t away = distance_to_decoration(s, true, row, 10);
            check(away >= 8, at + "the nearest ant decoration (rows that differ by 10 or more pixels) is " + std::to_string(away) + " rows away (at least 8)");
        } else {
            const auto run = identical_run(s, true, row);
            check(run.first <= 40 && run.second >= 80, at + "the chat box is a flat fill: the run of identical rows around the cut is " + std::to_string(run.first) + " - " + std::to_string(run.second));
        }
    }
    // every piece of the right panel that spans canvas row 357 is cut there: none is left whole across it
    {
        int spanning = 0;
        bool all_cut = true;
        for (size_t k = 0; k < 14; ++k) {
            const LayoutRect c = kPieces[k].classic;
            if (c.x < 458 || !(c.y <= 357 && 357 < c.bottom())) continue;
            ++spanning;
            all_cut = all_cut && kShellRules[k].cut_row >= 0 && c.y + kShellRules[k].cut_row == 357;
        }
        check(spanning == 3 && all_cut, "the pieces of the right panel that span canvas row 357 are exactly the three, and each is cut at it (" + std::to_string(spanning) + ")");
        // the rejected row: the right edge strip's row 50 (canvas y 304) stretches an ant decoration into a long bar
        const assets::Sprite& edge = sprite("x521y254.bmp");
        const bool plain_neighbours = line_diff(edge, true, 49, 50) <= 5 && line_diff(edge, true, 50, 51) <= 5;
        check(!(plain_neighbours && distance_to_decoration(edge, true, 50, 10) >= 8), "the control: x521y254 row 50, which was rejected, fails the same tests (it is beside a decoration)");
    }
    // the cuts together: the model's anchors and cut lines are the ones the independent mock-up uses
    for (const RefPiece& p : kReference) {
        const ShellRule& r = rule(p.name);
        check(r.cut_cols == p.cols && r.cut_row == p.row && r.right == p.right && r.bottom == p.bottom, std::string(p.name) + ": the model's anchors and cuts agree with the mock-up (" + std::to_string(r.cut_cols[0]) + ", " + std::to_string(r.cut_row) + ")");
    }
}

// =====================================================================================================================================================
// 3. The composed picture
// =====================================================================================================================================================

void test_composition(const assets::AssetArchive& arc) {
    group("compose", "the frame composed from the spans is the mock-up's one-line repeat, pixel for pixel, at every size; the original's picture is the plain 14 pieces");
    const struct { int32_t w, h; } sizes[] = {{960, 540}, {1280, 720}, {1920, 1080}, {700, 500}, {960, 480}, {640, 540}, {640, 480}, {960, 640}, {960, 600}, {1200, 540}, {1260, 540}, {1920, 540}};          // (every shape of the table)
    for (const auto& sz : sizes) {
        const Pic spans = compose_from_spans(arc, ScreenLayout::with_size(sz.w, sz.h));
        const Pic reference = compose_reference(arc, sz.w, sz.h);
        const int64_t diff = differing_pixels(spans, reference);
        check(diff == 0, std::to_string(sz.w) + " x " + std::to_string(sz.h) + ": the spans compose the mock-up's picture (" + std::to_string(diff) + " pixels differ)");
    }
    // the picture has exactly the holes of the original (the map view and the art's own openings): the stretch leaves none
    {
        const Pic wide = compose_from_spans(arc, ScreenLayout::with_size(960, 540));
        const Pic classic = compose_from_spans(arc, ScreenLayout::classic());
        int64_t holes_wide = 0, holes_classic = 0;
        for (int32_t y = 0; y < 540; ++y) {
            for (int32_t x = 0; x < 960; ++x) {
                const bool in_view = x >= 17 && x < 778 && y >= 22 && y < 521;
                if (!in_view && wide.at(x, y) < 0) ++holes_wide;
            }
        }
        for (int32_t y = 0; y < 480; ++y) {
            for (int32_t x = 0; x < 640; ++x) {
                const bool in_view = x >= 17 && x < 458 && y >= 22 && y < 461;
                if (!in_view && classic.at(x, y) < 0) ++holes_classic;
            }
        }
        check(holes_wide == holes_classic, "the wide frame has exactly the original's pixels that the art leaves open (" + std::to_string(holes_classic) + "), outside the map view");
    }
    // each stretched piece at its original size equals the original, and at the new size its added lines are the repeated one
    for (size_t k = 0; k < 14; ++k) {
        const ShellRule& rule = kShellRules[k];
        if (rule.cut_cols[0] < 0 && rule.cut_row < 0) continue;
        const assets::Sprite& s = *arc.find_sprite(rule.sprite);
        const LayoutRect c = kPieces[k].classic;
        // at the original's size: the spans are the whole piece, and drawing them gives the sprite
        Pic one(c.x + c.w + 4, c.y + c.h + 4);
        const ShellSpans spans0 = shell_spans(&rule, c.x, c.y, c.w, c.h, ScreenLayout::classic());
        for (size_t i = 0; i < spans0.count; ++i) blit_span(one, s, spans0.span[i].dst, spans0.span[i].src);
        bool equal = true;
        for (int32_t y = 0; y < c.h; ++y) {
            for (int32_t x = 0; x < c.w; ++x) {
                const uint8_t v = s.get_pixel(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
                equal = equal && one.at(c.x + x, c.y + y) == (v == assets::CHD_COLOR_KEY_INDEX ? int16_t{-1} : static_cast<int16_t>(v));
            }
        }
        check(equal, std::string(rule.sprite) + " drawn at its original size is the original");
        // at 960 x 540: lines before a cut are the original's, the added lines are the cut line, the lines after are the original's, moved (a piece with several cuts: dx shared between them)
        const ScreenLayout wide = ScreenLayout::with_size(960, 540);
        const ShellSpans spans = shell_spans(&rule, c.x, c.y, c.w, c.h, wide);
        Pic two(960, 540);
        for (size_t i = 0; i < spans.count; ++i) blit_span(two, s, spans.span[i].dst, spans.span[i].src);
        const int32_t ox = rule.right ? wide.dx() : 0, oy = rule.bottom ? wide.dy() : 0;
        bool lines_ok = true;
        std::string what;
        if (rule.cut_cols[0] >= 0) {
            std::vector<int32_t> cuts;
            for (const int32_t col : rule.cut_cols) {
                if (col >= 0) cuts.push_back(col);
            }
            const std::vector<std::pair<int32_t, int32_t>> columns = reference_columns(c.w, cuts, wide.dx());
            for (const auto& col : columns) {
                for (int32_t y = 0; y < c.h; ++y) {
                    const uint8_t v = s.get_pixel(static_cast<uint32_t>(col.first), static_cast<uint32_t>(y));
                    lines_ok = lines_ok && two.at(c.x + ox + col.second, c.y + oy + y) == (v == assets::CHD_COLOR_KEY_INDEX ? int16_t{-1} : static_cast<int16_t>(v));
                }
            }
            lines_ok = lines_ok && static_cast<int32_t>(columns.size()) == c.w + wide.dx();
            const std::vector<int32_t> shares = reference_shares(wide.dx(), cuts.size());
            what = "the columns before each cut, 1 + its share copies of the cut column (the shares:";
            for (const int32_t share : shares) what += " " + std::to_string(share);
            what += ") and the columns after it, in order";
        } else {
            const int32_t extra = wide.dy();
            const int32_t cut = rule.cut_row;
            for (int32_t line = 0; line < c.h + extra; ++line) {
                const int32_t src_line = line <= cut ? line : (line <= cut + extra ? cut : line - extra);       // the mock-up's rule
                for (int32_t i = 0; i < c.w; ++i) {
                    const uint8_t v = s.get_pixel(static_cast<uint32_t>(i), static_cast<uint32_t>(src_line));
                    lines_ok = lines_ok && two.at(c.x + ox + i, c.y + oy + line) == (v == assets::CHD_COLOR_KEY_INDEX ? int16_t{-1} : static_cast<int16_t>(v));
                }
            }
            what = "the lines before the cut, " + std::to_string(extra + 1) + " copies of the cut line and the lines after it, in order";
        }
        check(lines_ok, std::string(rule.sprite) + " at 960 x 540: " + what);
    }
    // the original's picture is the plain 14 pieces drawn the way the original draws them (draw_animation_frame0 order)
    {
        const Pic classic = compose_from_spans(arc, ScreenLayout::classic());
        Pic plain(640, 480);
        const auto& frames = arc.find_animation("uishell")->subitems[0].frames;
        for (size_t k = frames.size(); k-- > 0;) {
            const assets::Sprite& sp = arc.get_sprite(frames[k].sprite_index);
            blit_span(plain, sp, LayoutRect{frames[k].dx, frames[k].dy, static_cast<int32_t>(sp.width), static_cast<int32_t>(sp.height)},
                      LayoutRect{0, 0, static_cast<int32_t>(sp.width), static_cast<int32_t>(sp.height)});
        }
        check(differing_pixels(classic, plain) == 0, "the original's picture is the 14 plain pieces, the last stored first");
    }
}

void test_renderer_draws_spans(const assets::AssetArchive& arc) {
    group("renderer", "the real renderer draws the frame's spans exactly: the repeated lines, the pieces, the origin of a window");
    for (const auto& sz : {std::pair<int32_t, int32_t>{960, 540}, std::pair<int32_t, int32_t>{1280, 720}, std::pair<int32_t, int32_t>{960, 640}, std::pair<int32_t, int32_t>{960, 600}, std::pair<int32_t, int32_t>{1200, 540}, std::pair<int32_t, int32_t>{1260, 540},
                           std::pair<int32_t, int32_t>{1920, 540}}) {
        RendererRig rig(arc, sz.first, sz.second);
        check(rig.ok, "the renderer rig " + std::to_string(sz.first) + " x " + std::to_string(sz.second) + " is up");
        if (!rig.ok) continue;
        const ScreenLayout layout = ScreenLayout::with_size(sz.first, sz.second);
        rig.renderer.set_layout(layout);
        rig.renderer.set_hud_team(0);
        HUD hud;
        hud.set_layout(layout);
        rig.renderer.begin_frame();
        hud.render_shell(rig.renderer, arc);
        const std::vector<uint8_t> px = rig.read();
        // the expected picture: the mock-up's composition in the HUD's colors (every pixel the art does not cover is the black that the frame starts with)
        Pic reference = compose_reference(arc, sz.first, sz.second);
        // (who drew each pixel is needed for the color: compose again piece by piece, in the same order)
        std::vector<std::array<uint8_t, 4>> want(static_cast<size_t>(sz.first) * static_cast<size_t>(sz.second), std::array<uint8_t, 4>{0, 0, 0, 255});
        {
            const int32_t dw = sz.first - 640, dh = sz.second - 480;
            for (const RefPiece& p : kReference) {
                const assets::Sprite* sprite = arc.find_sprite(p.name);
                const int32_t ox = p.right ? dw : 0, oy = p.bottom ? dh : 0, eh = p.row >= 0 ? dh : 0;
                std::vector<int32_t> cuts;
                for (const int32_t c : p.cols) {
                    if (c >= 0) cuts.push_back(c);
                }
                const std::vector<std::pair<int32_t, int32_t>> columns = reference_columns(static_cast<int32_t>(sprite->width), cuts, dw);
                for (int32_t y = 0; y < static_cast<int32_t>(sprite->height) + eh; ++y) {
                    const int32_t sy = (eh > 0 && y > p.row) ? (y <= p.row + eh ? p.row : y - eh) : y;
                    for (const auto& col : columns) {
                        const uint8_t v = sprite->get_pixel(static_cast<uint32_t>(col.first), static_cast<uint32_t>(sy));
                        if (v == assets::CHD_COLOR_KEY_INDEX) continue;
                        const int32_t px_x = p.x + ox + col.second, px_y = p.y + oy + y;
                        if (px_x >= 0 && px_x < sz.first && px_y >= 0 && px_y < sz.second) want[static_cast<size_t>(px_y) * static_cast<size_t>(sz.first) + static_cast<size_t>(px_x)] = rgb_of(arc, p.name, v);
                    }
                }
            }
        }
        (void)reference;
        int64_t bad = 0;
        for (size_t i = 0; i < want.size(); ++i) {
            if (px[i * 4] != want[i][0] || px[i * 4 + 1] != want[i][1] || px[i * 4 + 2] != want[i][2]) ++bad;
        }
        check(bad == 0, std::to_string(sz.first) + " x " + std::to_string(sz.second) + ": the renderer's frame is the mock-up's, pixel for pixel (" + std::to_string(bad) + " pixels differ)");
    }
    // a part of a sprite is drawn where the picture and the origin put it
    {
        RendererRig rig(arc, 960, 540);
        if (rig.ok) {
            rig.renderer.set_hud_team(0);
            const int32_t id = arc.find_sprite_id("scorcovr.bmp");
            const assets::Sprite& cover = arc.get_sprite(static_cast<uint32_t>(id));
            auto drawn_at = [&](int32_t x0, int32_t y0) {            // the whole sprite stretched to twice its width, at (x0, y0) of the canvas: pixel (2 i, j) is the sprite's (i, j)
                const std::vector<uint8_t> px = rig.read();
                for (int32_t j = 0; j < static_cast<int32_t>(cover.height); ++j) {
                    for (int32_t i = 0; i < static_cast<int32_t>(cover.width); ++i) {
                        const uint8_t v = cover.get_pixel(static_cast<uint32_t>(i), static_cast<uint32_t>(j));
                        const std::array<uint8_t, 4> c = v == assets::CHD_COLOR_KEY_INDEX ? std::array<uint8_t, 4>{0, 0, 0, 255} : rgb_of(arc, "scorcovr.bmp", v);
                        for (int32_t k = 0; k < 2; ++k) {
                            const size_t at = (static_cast<size_t>(y0 + j) * 960 + static_cast<size_t>(x0 + 2 * i + k)) * 4;
                            if (px[at] != c[0] || px[at + 1] != c[1] || px[at + 2] != c[2]) return false;
                        }
                    }
                }
                return true;
            };
            rig.renderer.begin_frame();
            rig.renderer.draw_sprite_region(static_cast<uint32_t>(id), 10, 20, 2 * static_cast<int32_t>(cover.width), static_cast<int32_t>(cover.height), 0, 0, static_cast<int32_t>(cover.width), static_cast<int32_t>(cover.height));
            check(drawn_at(10, 20), "draw_sprite_region stretches a part of a sprite to the rectangle at its place");
            rig.renderer.begin_frame();
            rig.renderer.set_origin(30, 25);
            rig.renderer.draw_sprite_region(static_cast<uint32_t>(id), 10, 20, 2 * static_cast<int32_t>(cover.width), static_cast<int32_t>(cover.height), 0, 0, static_cast<int32_t>(cover.width), static_cast<int32_t>(cover.height));
            check(drawn_at(40, 45), "... moved by the origin");
            rig.renderer.set_origin(0, 0);
            rig.renderer.set_picture(LayoutRect{100, 40, 640, 480});
            rig.renderer.begin_frame();
            rig.renderer.draw_sprite_region(static_cast<uint32_t>(id), 10, 20, 2 * static_cast<int32_t>(cover.width), static_cast<int32_t>(cover.height), 0, 0, static_cast<int32_t>(cover.width), static_cast<int32_t>(cover.height));
            check(drawn_at(110, 60), "... and by the corner of the picture");
        }
    }
    // set_origin moves everything that is drawn after it, and nothing after it is reset
    {
        RendererRig rig(arc, 960, 540);
        if (!rig.ok) return;
        rig.renderer.set_hud_team(0);
        rig.renderer.begin_frame();
        rig.renderer.draw_named_sprite("scorcovr.bmp", 10, 10);
        rig.renderer.set_origin(100, 50);
        check(rig.renderer.origin() == LayoutPoint{100, 50}, "set_origin keeps the origin");
        rig.renderer.draw_named_sprite("scorcovr.bmp", 10, 10);
        rig.renderer.fill_rect(300, 300, 4, 4, assets::ColorRGBA{1, 2, 3, 255});
        rig.renderer.draw_rect(400, 300, 6, 6, assets::ColorRGBA{9, 8, 7, 255});
        rig.renderer.set_origin(0, 0);
        rig.renderer.draw_named_sprite("scorcovr.bmp", 500, 100);
        const std::vector<uint8_t> px = rig.read();
        const assets::Sprite& cover = *arc.find_sprite("scorcovr.bmp");
        auto pixel_at = [&](int32_t x, int32_t y) { return std::array<uint8_t, 3>{px[(static_cast<size_t>(y) * 960 + static_cast<size_t>(x)) * 4], px[(static_cast<size_t>(y) * 960 + static_cast<size_t>(x)) * 4 + 1], px[(static_cast<size_t>(y) * 960 + static_cast<size_t>(x)) * 4 + 2]}; };
        auto sprite_matches_at = [&](int32_t ox, int32_t oy) {
            for (int32_t y = 0; y < static_cast<int32_t>(cover.height); ++y) {
                for (int32_t x = 0; x < static_cast<int32_t>(cover.width); ++x) {
                    const uint8_t v = cover.get_pixel(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
                    const std::array<uint8_t, 4> c = v == assets::CHD_COLOR_KEY_INDEX ? std::array<uint8_t, 4>{0, 0, 0, 255} : rgb_of(arc, "scorcovr.bmp", v);
                    const auto p = pixel_at(ox + x, oy + y);
                    if (p[0] != c[0] || p[1] != c[1] || p[2] != c[2]) return false;
                }
            }
            return true;
        };
        check(sprite_matches_at(10, 10) && sprite_matches_at(110, 60) && sprite_matches_at(500, 100), "a sprite is drawn at its place, at its place plus the origin (100, 50), and again at its place after the origin is cleared");
        rig.renderer.set_origin(40, 40);
        rig.renderer.begin_frame();
        check(rig.renderer.origin() == LayoutPoint{0, 0}, "a new frame starts with no origin");
        check(pixel_at(300 + 100, 300 + 50) == (std::array<uint8_t, 3>{1, 2, 3}) && pixel_at(300, 300) != (std::array<uint8_t, 3>{1, 2, 3}), "a fill moves with the origin");
        check(pixel_at(400 + 100, 300 + 50) == (std::array<uint8_t, 3>{9, 8, 7}), "a frame moves with the origin");
        rig.renderer.begin_frame();
        rig.renderer.set_origin(7, 9);
        rig.renderer.set_clip_rect(0, 0, 10, 10);                        // a clip of a window is in the window's numbers too
        rig.renderer.fill_rect(0, 0, 50, 50, assets::ColorRGBA{200, 100, 50, 255});
        rig.renderer.clear_clip_rect();
        rig.renderer.set_origin(0, 0);
        const std::vector<uint8_t> px2 = rig.read();
        auto at2 = [&](int32_t x, int32_t y) { return std::array<uint8_t, 3>{px2[(static_cast<size_t>(y) * 960 + static_cast<size_t>(x)) * 4], px2[(static_cast<size_t>(y) * 960 + static_cast<size_t>(x)) * 4 + 1], px2[(static_cast<size_t>(y) * 960 + static_cast<size_t>(x)) * 4 + 2]}; };
        check(at2(7, 9) == (std::array<uint8_t, 3>{200, 100, 50}) && at2(16, 18) == (std::array<uint8_t, 3>{200, 100, 50}) && at2(17, 19) == (std::array<uint8_t, 3>{0, 0, 0}) && at2(6, 9) == (std::array<uint8_t, 3>{0, 0, 0}),
              "a clip set under an origin is the window's rectangle moved by it (10 x 10 at (7, 9))");
    }
}
