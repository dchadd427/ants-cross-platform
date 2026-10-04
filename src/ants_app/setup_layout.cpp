#include "ants_app/setup_layout.hpp"

#include <algorithm>
#include <array>

namespace ants::app {

namespace {

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// The strips: lines of the original's art that are drawn in runs. Every junction between two runs that do not follow each other in the piece is between lines that are identical (the line the
// first run ends with equals the line before the next run's first: the pair of lines at the junction is one that the piece itself contains), and every line that is repeated lies in a run of
// identical lines (tests/test_app/test_wide_setup.cpp checks each one against ants.chd). Found by the mock-up tool (a search for the fewest jumps between identical lines; the widened and narrowed pieces repeat or drop columns inside runs of identical columns).
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

// The bottom edges of the black boxes (efram4100: 5 rows of dithered shadow, 100 columns, tiled; the corners cover the first and the last 32 columns) and their side strips (efram2100 /
// efram3100: 4 columns, 100 rows, tiled)
constexpr LineSpan kPlayersBottomSpans[] = {{19, 81, 81}, {0, 63, 63}};                                      // 144 columns
constexpr LineSpan kPlayersSideSpans[] = {{0, 100, 100}, {0, 100, 100}, {0, 59, 59}, {60, 1, 1}};            // 260 rows
constexpr LineSpan kPreviewSingleBottomSpans[] = {{19, 81, 81}, {0, 100, 100}, {0, 63, 63}};                  // 244 columns
constexpr LineSpan kPreviewSingleSideSpans[] = {{0, 100, 100}, {0, 100, 100}, {0, 100, 100}};                 // 300 rows
constexpr LineSpan kPreviewOnlineBottomSpans[] = {{19, 81, 81}, {0, 100, 100}, {0, 11, 11}};                  // 192 columns
constexpr LineSpan kPreviewOnlineSideSpans[] = {{0, 100, 100}, {0, 100, 100}, {0, 44, 44}, {43, 4, 4}};       // 248 rows
constexpr LineSpan kChatBottomSpans[] = {{19, 81, 81}, {0, 72, 72}};                                          // 153 columns
constexpr LineSpan kChatSideSpans[] = {{0, 98, 98}, {99, 1, 1}};                                              // 99 rows
// The Map Info box's bottom edge starts at column 4 of efram4100 and ends at its column 87 (the construction of st_screen frames 9 .. 38)
constexpr LineSpan kInfoBottomSpans[] = {{4, 96, 96}, {0, 100, 100}, {0, 100, 100}, {0, 17, 17}, {18, 1, 1}};  // 314 columns

// w_map (195 x 39) and statline (313 x 42) made 70 columns wider: one column repeated 24, 23 and 23 times inside each of three runs of identical columns
constexpr LineSpan kMapBoxSpans[] = {{0, 29, 29}, {28, 1, 24}, {29, 40, 40}, {68, 1, 23}, {69, 101, 101}, {169, 1, 23}, {170, 25, 25}};                                    // 265 columns
constexpr LineSpan kStatusBoxSpans[] = {{0, 70, 70}, {69, 1, 24}, {70, 32, 32}, {101, 1, 23}, {102, 100, 100}, {201, 1, 23}, {202, 111, 111}};                              // 383 columns
// the chat input: statline's box (rows 11 .. 40) 96 columns narrower: runs of identical columns are drawn with fewer of their columns
constexpr LineSpan kChatInputSpans[] = {{0, 20, 20},  {20, 3, 3},   {30, 3, 3},   {38, 3, 3},   {45, 15, 15}, {60, 3, 3},   {70, 3, 3},   {78, 14, 14}, {92, 3, 3},   {102, 3, 3},  {110, 3, 3},
                                        {117, 28, 28}, {145, 3, 3},  {152, 3, 3},  {159, 4, 4},  {163, 3, 3},  {171, 3, 3},  {178, 14, 14}, {192, 3, 3},  {202, 3, 3},  {210, 4, 4},  {217, 28, 28},
                                        {245, 4, 4},  {252, 4, 4},  {259, 4, 4},  {263, 4, 4},  {271, 4, 4},  {278, 9, 9},  {287, 4, 4},  {295, 4, 4},  {302, 11, 11}};                                 // 217 columns

template <size_t N>
constexpr int32_t total_lines(const LineSpan (&spans)[N]) {
    int32_t sum = 0;
    for (size_t i = 0; i < N; ++i) sum += spans[i].dst_count;
    return sum;
}

#define ANTS_STRIP(id, sprite, columns, cross_first, cross_count, spans) \
    SetupStrip { id, sprite, columns, cross_first, cross_count, total_lines(spans), spans, sizeof(spans) / sizeof(spans[0]) }

const std::array<SetupStrip, 16> kStrips = {{
    ANTS_STRIP("box.players.bottom", "efram4100.bmp", true, 0, 0, kPlayersBottomSpans),
    ANTS_STRIP("box.players.left", "efram2100.bmp", false, 0, 0, kPlayersSideSpans),
    ANTS_STRIP("box.players.right", "efram3100.bmp", false, 0, 0, kPlayersSideSpans),
    ANTS_STRIP("box.preview_single.bottom", "efram4100.bmp", true, 0, 0, kPreviewSingleBottomSpans),
    ANTS_STRIP("box.preview_single.left", "efram2100.bmp", false, 0, 0, kPreviewSingleSideSpans),
    ANTS_STRIP("box.preview_single.right", "efram3100.bmp", false, 0, 0, kPreviewSingleSideSpans),
    ANTS_STRIP("box.preview_online.bottom", "efram4100.bmp", true, 0, 0, kPreviewOnlineBottomSpans),
    ANTS_STRIP("box.preview_online.left", "efram2100.bmp", false, 0, 0, kPreviewOnlineSideSpans),
    ANTS_STRIP("box.preview_online.right", "efram3100.bmp", false, 0, 0, kPreviewOnlineSideSpans),
    ANTS_STRIP("box.chat.bottom", "efram4100.bmp", true, 0, 0, kChatBottomSpans),
    ANTS_STRIP("box.chat.left", "efram2100.bmp", false, 0, 0, kChatSideSpans),
    ANTS_STRIP("box.chat.right", "efram3100.bmp", false, 0, 0, kChatSideSpans),
    ANTS_STRIP("box.info.bottom", "efram4100.bmp", true, 0, 0, kInfoBottomSpans),
    ANTS_STRIP("map_box", "w_map.bmp", true, 0, 0, kMapBoxSpans),
    ANTS_STRIP("status_box", "statline.bmp", true, 0, 0, kStatusBoxSpans),
    ANTS_STRIP("chat.input", "statline.bmp", true, 11, 30, kChatInputSpans),
}};

#undef ANTS_STRIP

const SetupStrip& strip(const char* id) { return find_strip(kStrips.data(), kStrips.size(), id); }

/// The black box of the Players' Status / preview / chat kind (the construction of st_screen frames 8 .. 24, Ants.exe): the outer rectangle is (iw + 8) x (ih + 9) at (x, y). At iw = ih = 200 it is the
/// original's box pixel for pixel; the top is one line repeated, the bottom edge and the sides are strips.
void draw_box_100(IRenderer& renderer, const ants::assets::AssetArchive& archive, int32_t x, int32_t y, int32_t iw, int32_t ih, const SetupStrip& bottom, const SetupStrip& left,
                  const SetupStrip& right) {
    const int32_t ow = iw + 8;
    renderer.fill_rect(x + 4, y + 4, iw, ih, kBoxBlack);
    draw_top_line(renderer, archive, x + 32, y, ow - 64);
    draw_strip(renderer, archive, bottom, x + 32, y + 4 + ih);
    draw_strip(renderer, archive, left, x, y + 4);
    draw_strip(renderer, archive, right, x + 4 + iw, y + 4);
    draw_piece(renderer, "efram1c.bmp", x, y);
    draw_piece(renderer, "efram2c.bmp", x + ow - 32, y);
    draw_piece(renderer, "efram3c.bmp", x, y + 4 + ih);
    draw_piece(renderer, "efram4c.bmp", x + ow - 32, y + 4 + ih);
}

/// The Map Info box (st_screen frames 9 .. 38): inner iw x 30, outer (iw + 9) x 38
void draw_box_info(IRenderer& renderer, const ants::assets::AssetArchive& archive, int32_t x, int32_t y, int32_t iw) {
    const int32_t ow = iw + 9;
    renderer.fill_rect(x + 4, y + 4, iw, 30, kBoxBlack);
    draw_top_line(renderer, archive, x + 32, y, ow - 64);
    draw_strip(renderer, archive, strip("box.info.bottom"), x + 33, y + 33);
    draw_piece(renderer, "efram2b.bmp", x, y + 3);
    draw_piece(renderer, "efram3b.bmp", x + 4 + iw, y + 3);
    draw_piece(renderer, "efram1c.bmp", x, y);
    draw_piece(renderer, "efram2c.bmp", x + ow - 32, y);
    draw_piece(renderer, "efram3c.bmp", x + 1, y + 33);
    draw_piece(renderer, "efram4c.bmp", x + ow - 32, y + 33);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// The three variants (the mock-ups' layout files: layout_C_single_*.txt, layout_C2_online_leader.txt, layout_C2_online_guest.txt)
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

SetupLayout make_common(SetupVariant variant) {
    SetupLayout l;
    l.variant = variant;
    l.title = {310, 0, 341, 34};
    l.map_box = {27, 362, 265, 39};
    l.info_label = {29, 408, 99, 22};
    l.info_box = {27, 433, 379, 38};
    l.status_box = {25, 489, 383, 42};
    l.name_text = {36, 372, 249, 18};
    l.info_text = {36, 440, 363, 18};
    l.prompt_text = {36, 507, 363, 14};
    l.players_label = {688, 55, 196, 23};
    l.players_box = {687, 79, 208, 269};
    l.fog_label = {687, 432, 142, 21};
    l.fog_text1 = {688, 456, 174, 15};
    l.fog_text2 = {687, 470, 182, 14};
    l.leave = {845, 12, 99, 22};
    l.seat_name_x = 735;
    l.seat_name_w = 120;
    l.seat_y = 95;
    l.seat_pitch = 50;
    l.thumb_x = 860;
    l.portrait_x = 715;
    l.portrait_y = 115;
    return l;
}

SetupLayout make_host(SetupVariant variant) {
    SetupLayout l = make_common(variant);
    l.art = {42, 99, 277, 175};
    l.map_label = {30, 338, 145, 20};
    l.up = {296, 359, 46, 22};
    l.down = {296, 383, 47, 20};
    l.fog_on = {842, 432, 49, 24};
    l.fog_off = {894, 432, 49, 24};
    l.start = {846, 499, 98, 27};
    return l;
}

SetupChatLayout make_chat() {
    SetupChatLayout c;
    c.label_x = 414;
    c.label_y = 363;
    c.box = {414, 386, 217, 108};
    c.lines = {418, 390, 209, 99};
    c.input_box = {414, 500, 217, 30};
    c.input_text = {424, 508, 197, 18};
    return c;
}

const SetupLayout& single_layout() {
    static const SetupLayout layout = [] {
        SetupLayout l = make_host(SetupVariant::Single);
        l.preview_box = {349, 46, 308, 309};
        l.preview_inner = 300;
        l.caption_centre_x = 349 + (300 + 8) / 2;
        l.caption_y = 46 + 300 + 9 + 5;
        return l;
    }();
    return layout;
}

const SetupLayout& online_layout() {
    static const SetupLayout layout = [] {
        SetupLayout l = make_host(SetupVariant::Online);
        l.preview_box = {375, 79, 256, 257};
        l.preview_inner = 248;
        l.caption_centre_x = 375 + (248 + 8) / 2;
        l.caption_y = 79 + 248 + 9 + 5;
        l.chat = make_chat();
        l.footer_x = 691;
        l.footer_y1 = 294;
        l.footer_y2 = 314;
        return l;
    }();
    return layout;
}

const SetupLayout& guest_layout() {
    static const SetupLayout layout = [] {
        SetupLayout l = make_common(SetupVariant::Guest);
        l.art = {20, 76, 315, 220};
        l.map_label = {25, 338, 61, 20};
        l.fog_fixed = {849, 421, 45, 30};
        l.fog_mark = {831, 432, 15, 18};
        l.preview_box = {375, 79, 256, 257};
        l.preview_inner = 248;
        l.caption_centre_x = 375 + (248 + 8) / 2;
        l.caption_y = 79 + 248 + 9 + 5;
        l.chat = make_chat();
        return l;
    }();
    return layout;
}

}  // namespace

const SetupStrip* setup_strips(size_t& count) noexcept {
    count = kStrips.size();
    return kStrips.data();
}

const SetupLayout& SetupLayout::of(SetupVariant variant) noexcept {
    switch (variant) {
        case SetupVariant::Online: return online_layout();
        case SetupVariant::Guest: return guest_layout();
        case SetupVariant::Single: break;
    }
    return single_layout();
}

void draw_setup_art(IRenderer& renderer, const ants::assets::AssetArchive& archive, const SetupArtOptions& options) {
    const SetupLayout& l = SetupLayout::of(options.variant);
    const bool guest = options.variant == SetupVariant::Guest;
    renderer.set_hud_team(0);
    draw_wide_background(renderer, archive, PageClay::Tiles);
    // the banner (it covers the top of the frame, as in the original), the left column
    draw_piece(renderer, guest ? "nhbanr.bmp" : "hostbanr.bmp", l.title.x, l.title.y);
    draw_piece(renderer, guest ? "waiting.bmp" : "gamesetup.bmp", l.art.x, l.art.y);
    draw_piece(renderer, guest ? "nhmap.bmp" : "pickmap.bmp", l.map_label.x, l.map_label.y);
    draw_strip(renderer, archive, strip("map_box"), l.map_box.x, l.map_box.y);
    draw_piece(renderer, "mapinfo.bmp", l.info_label.x, l.info_label.y);
    draw_box_info(renderer, archive, l.info_box.x, l.info_box.y, l.info_box.w - 9);
    draw_strip(renderer, archive, strip("status_box"), l.status_box.x, l.status_box.y);
    // the right column
    draw_piece(renderer, "playstat.bmp", l.players_label.x, l.players_label.y);
    draw_box_100(renderer, archive, l.players_box.x, l.players_box.y, SetupLayout::kBoxInnerPlayersW, SetupLayout::kBoxInnerPlayersH, strip("box.players.bottom"), strip("box.players.left"),
                 strip("box.players.right"));
    draw_piece(renderer, "fowar.bmp", l.fog_label.x, l.fog_label.y);
    draw_piece(renderer, "fowtext1.bmp", l.fog_text1.x, l.fog_text1.y);
    draw_piece(renderer, "fowtext2.bmp", l.fog_text2.x, l.fog_text2.y);
    if (guest) {
        draw_piece(renderer, "fowno.bmp", l.fog_fixed.x, l.fog_fixed.y);
        draw_piece(renderer, "Q_mark.bmp", l.fog_mark.x, l.fog_mark.y);
    }
    // the map preview's black box
    if (options.variant == SetupVariant::Single) {
        draw_box_100(renderer, archive, l.preview_box.x, l.preview_box.y, l.preview_inner, l.preview_inner, strip("box.preview_single.bottom"), strip("box.preview_single.left"),
                     strip("box.preview_single.right"));
    } else {
        draw_box_100(renderer, archive, l.preview_box.x, l.preview_box.y, l.preview_inner, l.preview_inner, strip("box.preview_online.bottom"), strip("box.preview_online.left"),
                     strip("box.preview_online.right"));
    }
    // the chat column's frame: the black chat box and the input box
    if (options.chat_frame && l.chat.valid()) {
        draw_box_100(renderer, archive, l.chat.box.x, l.chat.box.y, l.chat.lines.w, l.chat.lines.h, strip("box.chat.bottom"), strip("box.chat.left"), strip("box.chat.right"));
        draw_strip(renderer, archive, strip("chat.input"), l.chat.input_box.x, l.chat.input_box.y);
    }
}

}  // namespace ants::app
