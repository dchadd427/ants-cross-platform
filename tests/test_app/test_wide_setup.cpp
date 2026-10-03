// The wide setup screen (the setup / map selection screen of the original recomposed for the 16:9 picture of 960 x 540, with a map preview; include/ants_app/setup_layout.hpp,
// map_preview.hpp, src/ants_app/map_select_wide.cpp):
//   1. the layout: every rectangle of every variant (the host's screen of a local game, of a network game, the guest's screen) is the owner-approved mock-up's (the layout files that the mock-up
//      tool wrote beside its pictures: every element's rectangle at 960 x 540), and the chat column does not touch anything else;
//   2. the seams: every strip of art that the screen draws from runs of lines (the frame's side strips, the bottom and the sides of the black boxes, the widened map list box and
//      status box, the narrowed chat input box) is checked against the art of ants.chd: every junction of two runs that do not follow each other is between lines that are identical,
//      every repeated line lies in a run of identical lines, the strips have the lengths the screen needs, and start and end where the original's own strips do;
//   3. the composed screens against the mock-up PNGs, pixel for pixel: the real renderer draws each variant; the picture is compared with the mock-up (pinned FNV-1a 64 digests of the
//      mock-ups' own pixels) with the text areas masked, the map preview separately;
//   4. the map preview: the size rule (a whole scale per cell from two pixels up, a nearest-neighbour sample below), the pictures of TINY, GAUNTLET and ISLANDS (the mock-ups' own
//      pixels), a big synthetic level (sampled; an independent oracle), plants as dots, the caption;
//   5. the screens' state: what is drawn where (labels, seats, thumbs, the chat column and the bot-fill footer, only when they are asked for), the "No preview" box;
//   6. the pointer: through the application, every button of every variant answers at the rectangle of the layout (and only there), the classic page answers where it always did;
//   7. the application: the picture of the setup screen is the whole 960 x 540 canvas (and so is every other screen: test_wide_pages), the classic 4:3 game is the original's;
//   8. fingerprints: the draw calls and the masked pixels of the wide screens in a dozen states, and what a click does at every pixel of the picture (golden numbers of this commit).
// Usage: test_wide_setup [--print]   (--print writes the golden table of the fingerprints for regeneration; never to silence a failure). Exit code 0 when every check passes.
#include <SDL.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "ants_app/application.hpp"
#include "ants_app/canvas_layout.hpp"
#include "ants_app/map_preview.hpp"
#include "ants_app/map_select.hpp"
#include "ants_app/minimap_tables.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/setup_layout.hpp"
#include "ants_assets/asset_archive.hpp"
#include "ants_assets/lvl_parser.hpp"
#include "ants_sim/grid.hpp"
#include "ants_sim/sim_engine.hpp"

using namespace ants;
using namespace ants::app;

#ifndef ORIGINAL_ASSETS_DIR
#define ORIGINAL_ASSETS_DIR "Original-Ants"
#endif

namespace {

int g_checks = 0;
int g_failures = 0;
const char* g_group = "";
bool g_print = false;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::fprintf(stderr, "  FAIL [%s]: %s\n", g_group, what.c_str());
    }
}

void group(const char* name, const char* what) {
    g_group = name;
    std::printf("[%s] %s\n", name, what);
}

std::string str(const LayoutRect& r) {
    std::ostringstream o;
    o << "(" << r.x << ", " << r.y << ", " << r.w << " x " << r.h << ")";
    return o.str();
}

void check_rect(const LayoutRect& got, const LayoutRect& want, const std::string& what) { check(got == want, what + ": " + str(got) + " should be " + str(want)); }

std::string hex64(uint64_t v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%016llx", static_cast<unsigned long long>(v));
    return buf;
}

void ensure_sdl() {
    SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
    if (SDL_WasInit(SDL_INIT_VIDEO) == 0) SDL_Init(SDL_INIT_VIDEO);
}

/// The renderer and the application print what font they found on std::cout: that is not of interest here
class QuietStdout {
public:
    QuietStdout() : old_(std::cout.rdbuf(sink_.rdbuf())) {}
    ~QuietStdout() { std::cout.rdbuf(old_); }
    QuietStdout(const QuietStdout&) = delete;
    QuietStdout& operator=(const QuietStdout&) = delete;

private:
    std::ostringstream sink_;
    std::streambuf* old_;
};

/// What a screen logs on std::cerr (the map preview says once that it fell back to the minimap colours) is of no interest to the tests that use a renderer that cannot draw offscreen; it is
/// kept, and test_map_preview checks it. (Failures are printed on the C stream stderr, which this does not touch.)
class QuietCerr {
public:
    QuietCerr() : old_(std::cerr.rdbuf(sink_.rdbuf())) {}
    ~QuietCerr() { std::cerr.rdbuf(old_); }
    QuietCerr(const QuietCerr&) = delete;
    QuietCerr& operator=(const QuietCerr&) = delete;

private:
    std::ostringstream sink_;
    std::streambuf* old_;
};

/// FNV-1a 64 over bytes
class Fnv {
public:
    void byte(uint8_t b) {
        h_ ^= b;
        h_ *= 0x100000001b3ull;
    }
    void i32(int32_t v) {
        for (int i = 0; i < 4; ++i) byte(static_cast<uint8_t>((static_cast<uint32_t>(v) >> (8 * i)) & 0xffu));
    }
    void text(const std::string& s) {
        i32(static_cast<int32_t>(s.size()));
        for (char c : s) byte(static_cast<uint8_t>(c));
    }
    uint64_t value() const { return h_; }

private:
    uint64_t h_{0xcbf29ce484222325ull};
};

// =====================================================================================================================================================
// The mock-ups' rectangles (their layout files, element by element, as the mock-up tool wrote them)
// =====================================================================================================================================================

struct MockRect {
    const char* name;
    int32_t x, y, w, h;
};

constexpr MockRect kMockSingle[] = {
    {"title banner", 310, 0, 341, 34},
    {"GAME SET UP art", 42, 99, 277, 175},
    {"Pick a Map label", 30, 338, 145, 20},
    {"map list box (map name)", 27, 362, 265, 39},
    {"Up button", 296, 359, 46, 22},
    {"Down button", 296, 383, 47, 20},
    {"Map Info label", 29, 408, 99, 22},
    {"Map Info box", 27, 433, 379, 38},
    {"status box (with its \"status\" label)", 25, 489, 383, 42},
    {"map name label", 36, 372, 249, 18},
    {"map description label", 36, 440, 363, 18},
    {"prompt label", 36, 507, 363, 14},
    {"Players' Status label", 688, 55, 196, 23},
    {"Players' Status box", 687, 79, 208, 269},
    {"Fog of War label", 687, 432, 142, 21},
    {"fog text line 1", 688, 456, 174, 15},
    {"fog text line 2", 687, 470, 182, 14},
    {"Fog On button", 842, 432, 49, 24},
    {"Fog Off button", 894, 432, 49, 24},
    {"START button", 846, 499, 98, 27},
    {"Leave Game button", 845, 12, 99, 22},
    {"seat 1 name", 735, 95, 120, 18},
    {"Map preview box", 349, 46, 308, 309},
    {"preview caption", 422, 360, 162, 18},
};
constexpr MockRect kMockOnline[] = {
    {"title banner", 310, 0, 341, 34},
    {"GAME SET UP art", 42, 99, 277, 175},
    {"Pick a Map label", 30, 338, 145, 20},
    {"map list box (map name)", 27, 362, 265, 39},
    {"Up button", 296, 359, 46, 22},
    {"Down button", 296, 383, 47, 20},
    {"Map Info label", 29, 408, 99, 22},
    {"Map Info box", 27, 433, 379, 38},
    {"status box (with its \"status\" label)", 25, 489, 383, 42},
    {"map name label", 36, 372, 249, 18},
    {"map description label", 36, 440, 363, 18},
    {"prompt label", 36, 507, 363, 14},
    {"Players' Status label", 688, 55, 196, 23},
    {"Players' Status box", 687, 79, 208, 269},
    {"Fog of War label", 687, 432, 142, 21},
    {"fog text line 1", 688, 456, 174, 15},
    {"fog text line 2", 687, 470, 182, 14},
    {"Fog On button", 842, 432, 49, 24},
    {"Fog Off button", 894, 432, 49, 24},
    {"START button", 846, 499, 98, 27},
    {"Leave Game button", 845, 12, 99, 22},
    {"seat 1 name", 735, 95, 120, 18},
    {"seat 2 name", 735, 145, 120, 18},
    {"seat 3 name", 735, 195, 120, 18},
    {"seat footer line 1", 691, 294, 164, 18},
    {"seat footer line 2", 691, 314, 92, 18},
    {"Map preview box", 375, 79, 256, 257},
    {"preview caption", 418, 341, 170, 18},
    {"Chat label", 414, 363, 37, 20},
    {"Chat box", 414, 386, 217, 108},
    {"chat lines area", 418, 390, 209, 99},
    {"chat input box", 414, 500, 217, 30},
    {"chat input text", 424, 508, 197, 18},
};
constexpr MockRect kMockGuest[] = {
    {"title banner", 310, 0, 341, 34},
    {"WAITING FOR GAME TO START art", 20, 76, 315, 220},
    {"Map label", 25, 338, 61, 20},
    {"map list box (map name)", 27, 362, 265, 39},
    {"Map Info label", 29, 408, 99, 22},
    {"Map Info box", 27, 433, 379, 38},
    {"status box (with its \"status\" label)", 25, 489, 383, 42},
    {"map name label", 36, 372, 249, 18},
    {"map description label", 36, 440, 363, 18},
    {"prompt label", 36, 507, 363, 14},
    {"Players' Status label", 688, 55, 196, 23},
    {"Players' Status box", 687, 79, 208, 269},
    {"Fog of War label", 687, 432, 142, 21},
    {"fog text line 1", 688, 456, 174, 15},
    {"fog text line 2", 687, 470, 182, 14},
    {"fixed Fog box (No)", 849, 421, 45, 30},
    {"Fog ? mark", 831, 432, 15, 18},
    {"Leave Game button", 845, 12, 99, 22},
    {"seat 1 name", 735, 95, 120, 18},
    {"seat 2 name", 735, 145, 120, 18},
    {"seat 3 name", 735, 195, 120, 18},
    {"Map preview box", 375, 79, 256, 257},
    {"preview caption", 418, 341, 170, 18},
    {"Chat label", 414, 363, 37, 20},
    {"Chat box", 414, 386, 217, 108},
    {"chat lines area", 418, 390, 209, 99},
    {"chat input box", 414, 500, 217, 30},
    {"chat input text", 424, 508, 197, 18},
};

/// How a mock-up's rectangle is compared with the layout's
enum class Cmp { Exact, Origin, Caption };
struct Mapped {
    LayoutRect rect;
    Cmp cmp{Cmp::Exact};
};

std::optional<Mapped> map_mock(const SetupLayout& l, const std::string& name) {
    using R = LayoutRect;
    if (name == "title banner") return Mapped{l.title};
    if (name == "GAME SET UP art" || name == "WAITING FOR GAME TO START art") return Mapped{l.art};
    if (name == "Pick a Map label" || name == "Map label") return Mapped{l.map_label};
    if (name == "map list box (map name)") return Mapped{l.map_box};
    if (name == "Up button") return Mapped{l.up};
    if (name == "Down button") return Mapped{l.down};
    if (name == "Map Info label") return Mapped{l.info_label};
    if (name == "Map Info box") return Mapped{l.info_box};
    if (name == "status box (with its \"status\" label)") return Mapped{l.status_box};
    if (name == "map name label") return Mapped{l.name_text};
    if (name == "map description label") return Mapped{l.info_text};
    if (name == "prompt label") return Mapped{l.prompt_text};
    if (name == "Players' Status label") return Mapped{l.players_label};
    if (name == "Players' Status box") return Mapped{l.players_box};
    if (name == "Fog of War label") return Mapped{l.fog_label};
    if (name == "fog text line 1") return Mapped{l.fog_text1};
    if (name == "fog text line 2") return Mapped{l.fog_text2};
    if (name == "Fog On button") return Mapped{l.fog_on};
    if (name == "Fog Off button") return Mapped{l.fog_off};
    if (name == "START button") return Mapped{l.start};
    if (name == "Leave Game button") return Mapped{l.leave};
    if (name == "fixed Fog box (No)") return Mapped{l.fog_fixed};
    if (name == "Fog ? mark") return Mapped{l.fog_mark};
    if (name.rfind("seat ", 0) == 0 && name.size() > 5 && name.find(" name") != std::string::npos) {
        const int row = name[5] - '1';
        return Mapped{R{l.seat_name_x, l.seat_y + l.seat_pitch * row, l.seat_name_w, 18}};
    }
    if (name == "Map preview box") return Mapped{l.preview_box};
    if (name == "preview caption") return Mapped{R{l.caption_centre_x, l.caption_y, 0, 18}, Cmp::Caption};
    if (name == "seat footer line 1") return Mapped{R{l.footer_x, l.footer_y1, 0, 0}, Cmp::Origin};
    if (name == "seat footer line 2") return Mapped{R{l.footer_x, l.footer_y2, 0, 0}, Cmp::Origin};
    if (name == "Chat label") return Mapped{R{l.chat.label_x, l.chat.label_y, 0, 0}, Cmp::Origin};
    if (name == "Chat box") return Mapped{l.chat.box};
    if (name == "chat lines area") return Mapped{l.chat.lines};
    if (name == "chat input box") return Mapped{l.chat.input_box};
    if (name == "chat input text") return Mapped{l.chat.input_text};
    return std::nullopt;
}

template <size_t N>
void check_mock_table(const MockRect (&table)[N], SetupVariant variant, const char* label) {
    const SetupLayout& l = SetupLayout::of(variant);
    check(l.variant == variant, std::string(label) + ": the layout knows its variant");
    for (const MockRect& m : table) {
        const std::optional<Mapped> mapped = map_mock(l, m.name);
        check(mapped.has_value(), std::string(label) + ": the layout has an element for the mock-up's \"" + m.name + "\"");
        if (!mapped) continue;
        const std::string what = std::string(label) + ": " + m.name;
        switch (mapped->cmp) {
            case Cmp::Exact: check_rect(mapped->rect, LayoutRect{m.x, m.y, m.w, m.h}, what); break;
            case Cmp::Origin: check(mapped->rect.x == m.x && mapped->rect.y == m.y, what + ": the text starts at (" + std::to_string(mapped->rect.x) + ", " + std::to_string(mapped->rect.y) + "), the mock-up's is (" + std::to_string(m.x) + ", " + std::to_string(m.y) + ")"); break;
            case Cmp::Caption:                                  // centred on the preview box's middle: the mock-up's text (its own width) is within a pixel of it
                check(mapped->rect.y == m.y && std::abs(m.x + m.w / 2 - mapped->rect.x) <= 1, what + ": under the preview, centred (centre " + std::to_string(mapped->rect.x) + ", the mock-up's " + std::to_string(m.x + m.w / 2) + ")");
                break;
        }
    }
}

bool overlaps(const LayoutRect& a, const LayoutRect& b) { return a.w > 0 && a.h > 0 && b.w > 0 && b.h > 0 && a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom(); }

void test_layout() {
    group("layout", "every rectangle of every variant of the wide setup screen is the mock-up's");
    check_mock_table(kMockSingle, SetupVariant::Single, "single");
    check_mock_table(kMockOnline, SetupVariant::Online, "online host");
    check_mock_table(kMockGuest, SetupVariant::Guest, "guest");
    // what the numbers add up to: the right column is the original's moved by 320, the bottom groups by 60 (the owner's mock-up: "just wider" plus the preview)
    check(SetupLayout::kRightDx == 960 - 640 && SetupLayout::kBottomDy == 540 - 480 && SetupLayout::kListDx == 70, "the moves are the canvas's extra width and height, and the map list's 70 columns");
    check(SetupLayout::supports(960, 540) && !SetupLayout::supports(640, 480) && !SetupLayout::supports(1280, 720) && !SetupLayout::supports(960, 541), "the wide screen is made for 960 x 540 and nothing else");
    const SetupLayout& s = SetupLayout::of(SetupVariant::Single);
    const SetupLayout& o = SetupLayout::of(SetupVariant::Online);
    const SetupLayout& g = SetupLayout::of(SetupVariant::Guest);
    check(s.has_host_controls() && o.has_host_controls() && !g.has_host_controls(), "the host's screens have the map, fog and START controls, the guest's none");
    check(!s.chat.valid() && o.chat.valid() && g.chat.valid(), "the chat column is on the network screens only");
    check(s.footer_x == 0 && o.footer_x == 691 && g.footer_x == 0, "the bot-fill footer is on the host's network screen only");
    check(s.preview_inner == 300 && o.preview_inner == 248 && g.preview_inner == 248, "the preview is 300 square without a chat, 248 beside the chat column");
    check_rect(s.preview_area(), LayoutRect{353, 50, 300, 300}, "single: the preview's inner square");
    check_rect(o.preview_area(), LayoutRect{379, 83, 248, 248}, "online: the preview's inner square");
    check_rect(s.preview_picture(279, 279), LayoutRect{363, 60, 279, 279}, "single: a 279 square picture (TINY) is centred in it");
    check_rect(o.preview_picture(240, 240), LayoutRect{383, 87, 240, 240}, "online: a 240 square picture is centred in it");
    check_rect(o.preview_picture(248, 198), LayoutRect{379, 108, 248, 198}, "online: a wide picture is centred in it");
    check(s.caption_y == 360 && o.caption_y == 341 && s.caption_centre_x == 503 && o.caption_centre_x == 503 && g.caption_centre_x == 503, "the captions are under the preview boxes, centred on the picture's middle (503)");
    check(s.seat_name_x == 735 && s.thumb_x == 860 && s.portrait_x == 715 && s.portrait_y == 115 && s.seat_pitch == 50 && s.seat_y == 95, "the players' rows: the original's (415, 540, 395) + 320, rows 50 apart");
    check(s.seat_name_x == SetupLayout::kRightDx + MapSelectScreen::PLAYER_NAME_X && s.thumb_x == SetupLayout::kRightDx + MapSelectScreen::PLAYER_THUMB_X &&
              s.portrait_x == SetupLayout::kRightDx + MapSelectScreen::PLAYER_PORTRAIT_X && s.portrait_y == MapSelectScreen::PLAYER_PORTRAIT_Y && s.seat_pitch == MapSelectScreen::PLAYER_ROW_PITCH,
          "... and so they are the classic page's constants moved by the right column's 320");
    // the chat column: its pieces stand in order, inside the picture, clear of the preview, the left column, the right column and the Fog / START corner
    for (const SetupLayout* l : {&o, &g}) {
        const SetupChatLayout& c = l->chat;
        const LayoutRect canvas{0, 0, 960, 540};
        const LayoutRect label{c.label_x, c.label_y, 40, 20};
        check(c.box.x >= canvas.x && c.box.right() <= canvas.right() && c.input_box.bottom() <= canvas.bottom() && label.y >= l->caption_y + 18, "the chat column lies inside the picture, below the caption");
        check(c.lines.x == c.box.x + 4 && c.lines.y == c.box.y + 4 && c.lines.w == c.box.w - 8 && c.lines.h == c.box.h - 9, "the lines' area is the inside of the black box (the efram construction: 4 left / top, 4 right, 5 bottom)");
        check(c.box.right() == 631 && l->preview_box.right() == 631 && c.input_box.right() == 631, "the chat column's right edge is the preview box's right edge (631)");
        check(c.input_box.x == c.box.x && c.input_box.w == c.box.w && c.input_box.y > c.box.bottom() && c.input_box.h == 30, "the input box is under the chat box, as wide, 30 high (statline's box)");
        check(c.input_text.x == c.input_box.x + 10 && c.input_text.w == c.input_box.w - 20, "the typed text starts 10 inside the input box");
        // the input box's top is that of the status box's black box: rows 11 .. 40 of statline start 11 below the status box's top
        check(c.input_box.y == l->status_box.y + 11 && c.input_box.bottom() == l->status_box.y + 41, "the input box is on the status box's own rows (its black box, without the label)");
        for (const LayoutRect* other : {&l->info_box, &l->status_box, &l->map_box, &l->preview_box, &l->players_box, &l->fog_label, &l->fog_text1, &l->fog_text2, &l->leave, &l->players_label}) {
            check(!overlaps(c.box, *other) && !overlaps(c.input_box, *other) && !overlaps(label, *other), "the chat column touches no other element of the screen " + str(*other));
        }
        if (l->has_host_controls()) {
            check(!overlaps(c.box, l->start) && !overlaps(c.input_box, l->start) && !overlaps(c.box, l->fog_on) && !overlaps(c.input_box, l->fog_off), "... nor the host's controls");
        }
    }
    // the preview box is clear of the other elements too
    for (const SetupLayout* l : {&s, &o, &g}) {
        for (const LayoutRect* other : {&l->info_box, &l->status_box, &l->map_box, &l->players_box, &l->players_label, &l->leave, &l->fog_label}) {
            check(!overlaps(l->preview_box, *other), "the preview box touches no other element of the screen " + str(*other));
        }
    }
    // the footer lines are inside the Players' Status box
    check(o.footer_x >= o.players_box.x + 4 && o.footer_y1 > o.seat_y + 3 * o.seat_pitch + 18 && o.footer_y2 + 18 <= o.players_box.y + 4 + SetupLayout::kBoxInnerPlayersH, "the bot-fill footer lies in the Players' Status box below the fourth row");
    // the art that the left column and the preview share
    check(overlaps(s.art, s.map_box) == false && overlaps(s.art, s.preview_box) == false && overlaps(g.art, g.preview_box) == false, "the title art does not reach the preview");
}

// =====================================================================================================================================================
// 2. The seams
// =====================================================================================================================================================

/// Are two lines of a sprite (columns or rows, over the cross range) identical, pixel for pixel (the colour key included)?
bool lines_identical(const assets::Sprite& s, bool columns, int32_t a, int32_t b, int32_t cross_first, int32_t cross_count) {
    const int32_t cross = cross_count > 0 ? cross_count : (columns ? static_cast<int32_t>(s.height) : static_cast<int32_t>(s.width));
    for (int32_t i = 0; i < cross; ++i) {
        const int32_t c = cross_first + i;
        const uint8_t pa = columns ? s.get_pixel(static_cast<uint32_t>(a), static_cast<uint32_t>(c)) : s.get_pixel(static_cast<uint32_t>(c), static_cast<uint32_t>(a));
        const uint8_t pb = columns ? s.get_pixel(static_cast<uint32_t>(b), static_cast<uint32_t>(c)) : s.get_pixel(static_cast<uint32_t>(c), static_cast<uint32_t>(b));
        if (pa != pb) return false;
    }
    return true;
}

struct ExpectedStrip {
    const char* id;
    const char* sprite;
    bool columns;
    int32_t length;      // the lines of the picture the strip makes
    int32_t first;       // the line it starts with (the original's own strip starts there)
    int32_t last;        // the line that the last displayed line equals (where the original's own strip ends: the corner joins there)
    int32_t jumps;       // the junctions between runs that do not follow each other (each of them is checked to be between identical lines)
    int32_t repeats;     // the lines that are repeated
};

constexpr ExpectedStrip kExpectedStrips[] = {
    {"frame.left", "dfram496.bmp", false, 508, 0, 95, 1, 0},
    {"frame.right", "dfram596.bmp", false, 508, 0, 95, 1, 0},
    {"box.players.bottom", "efram4100.bmp", true, 144, 19, 71, 0, 0},
    {"box.players.left", "efram2100.bmp", false, 260, 0, 99, 1, 0},
    {"box.players.right", "efram3100.bmp", false, 260, 0, 99, 1, 0},
    {"box.preview_single.bottom", "efram4100.bmp", true, 244, 19, 71, 0, 0},
    {"box.preview_single.left", "efram2100.bmp", false, 300, 0, 99, 0, 0},
    {"box.preview_single.right", "efram3100.bmp", false, 300, 0, 99, 0, 0},
    {"box.preview_online.bottom", "efram4100.bmp", true, 192, 19, 71, 0, 0},
    {"box.preview_online.left", "efram2100.bmp", false, 248, 0, 99, 1, 0},
    {"box.preview_online.right", "efram3100.bmp", false, 248, 0, 99, 1, 0},
    {"box.chat.bottom", "efram4100.bmp", true, 153, 19, 71, 0, 0},
    {"box.chat.left", "efram2100.bmp", false, 99, 0, 99, 1, 0},
    {"box.chat.right", "efram3100.bmp", false, 99, 0, 99, 1, 0},
    {"box.info.bottom", "efram4100.bmp", true, 314, 4, 87, 1, 0},
    {"map_box", "w_map.bmp", true, 265, 0, 194, 3, 3},
    {"status_box", "statline.bmp", true, 383, 0, 312, 3, 3},
    {"chat.input", "statline.bmp", true, 217, 0, 312, 21, 0},
};

void test_strips(const assets::AssetArchive& arc) {
    group("seams", "every strip of art: lengths, the seams between runs (identical lines only), the repeated lines (inside runs of identical lines), start and end");
    // the screen's own 16 strips and the two side strips of the frame, which the shared wide background draws (wide_page.hpp: the frame moved there so that the other wide pages stand on it too)
    size_t own = 0, shared = 0;
    const SetupStrip* own_strips = setup_strips(own);
    const SetupStrip* shared_strips = wide_page_strips(shared);
    std::vector<SetupStrip> all(own_strips, own_strips + own);
    all.insert(all.end(), shared_strips, shared_strips + shared);
    const SetupStrip* strips = all.data();
    const size_t count = all.size();
    check(own == 16 && shared == 2, "the screen has 16 strips of its own and shares the frame's two");
    check(count == sizeof(kExpectedStrips) / sizeof(kExpectedStrips[0]), "the screen draws " + std::to_string(sizeof(kExpectedStrips) / sizeof(kExpectedStrips[0])) + " strips (" + std::to_string(count) + ")");
    for (const ExpectedStrip& want : kExpectedStrips) {
        const SetupStrip* found = nullptr;
        for (size_t i = 0; i < count; ++i) {
            if (std::strcmp(strips[i].id, want.id) == 0) found = &strips[i];
        }
        check(found != nullptr, std::string("the strip ") + want.id + " exists");
        if (found == nullptr) continue;
        const SetupStrip& s = *found;
        const std::string at = std::string("strip ") + want.id + ": ";
        check(std::strcmp(s.sprite, want.sprite) == 0 && s.columns == want.columns, at + "the piece " + want.sprite + (want.columns ? " by columns" : " by rows"));
        check(s.length == want.length, at + "makes " + std::to_string(want.length) + " lines (" + std::to_string(s.length) + ")");
        const assets::Sprite* sprite = arc.find_sprite(s.sprite);
        check(sprite != nullptr, at + "the piece exists in the archive");
        if (sprite == nullptr || s.span_count == 0) continue;
        const int32_t n = s.columns ? static_cast<int32_t>(sprite->width) : static_cast<int32_t>(sprite->height);
        const int32_t cross_max = s.columns ? static_cast<int32_t>(sprite->height) : static_cast<int32_t>(sprite->width);
        const int32_t cross = s.cross_count > 0 ? s.cross_count : cross_max;
        check(s.cross_first >= 0 && s.cross_first + cross <= cross_max && cross > 0, at + "the cross range lies inside the piece");
        int32_t sum = 0;
        bool inside = true, shapes = true;
        for (size_t i = 0; i < s.span_count; ++i) {
            const LineSpan& sp = s.spans[i];
            sum += sp.dst_count;
            inside = inside && sp.src >= 0 && sp.src_count >= 1 && sp.src + sp.src_count <= n && sp.dst_count >= 1;
            shapes = shapes && (sp.dst_count == sp.src_count || (sp.src_count == 1 && sp.dst_count > 1));
        }
        check(sum == s.length, at + "the spans add up to its length (" + std::to_string(sum) + ")");
        check(inside, at + "every span reads inside the piece");
        check(shapes, at + "a span is a plain copy or one line repeated");
        check(s.spans[0].src == want.first, at + "it starts with line " + std::to_string(want.first) + " (the original's own strip starts there)");
        // the junctions: from the last line a span shows to the first line of the next
        int32_t bad_junctions = 0, bad_repeats = 0, jumps = 0;
        for (size_t i = 0; i < s.span_count; ++i) {
            const LineSpan& sp = s.spans[i];
            if (sp.dst_count != sp.src_count) {                     // a repeated line: it lies in a run of identical lines (the run only grows)
                const int32_t c = sp.src;
                const bool in_run = (c > 0 && lines_identical(*sprite, s.columns, c - 1, c, s.cross_first, s.cross_count)) || (c + 1 < n && lines_identical(*sprite, s.columns, c, c + 1, s.cross_first, s.cross_count));
                if (!in_run) ++bad_repeats;
            }
            if (i + 1 < s.span_count) {
                const int32_t e = sp.dst_count != sp.src_count ? sp.src : sp.src + sp.src_count - 1;     // the last line this span shows
                const int32_t f = s.spans[i + 1].src;                                                        // the first line of the next
                const bool follows = f == e + 1 || (e == n - 1 && f == 0);                                   // the piece's own order (a wrap is the original's own join)
                if (!follows) {
                    ++jumps;
                    // a jump is free when the line e equals the line before f: the pair (e, f) is then the pair (f - 1, f) that the piece itself holds
                    if (f < 1 || !lines_identical(*sprite, s.columns, e, f - 1, s.cross_first, s.cross_count)) ++bad_junctions;
                }
            }
        }
        int32_t repeats_seen = 0;
        for (size_t i = 0; i < s.span_count; ++i) repeats_seen += s.spans[i].dst_count != s.spans[i].src_count ? 1 : 0;
        check(jumps == want.jumps && repeats_seen == want.repeats, at + std::to_string(jumps) + " junctions that jump and " + std::to_string(repeats_seen) + " repeated lines, wanted " + std::to_string(want.jumps) + " and " + std::to_string(want.repeats));
        check(bad_junctions == 0, at + "every junction between runs that do not follow each other is between identical lines (" + std::to_string(bad_junctions) + " are not, of " + std::to_string(jumps) + " jumps)");
        check(bad_repeats == 0, at + "every repeated line lies in a run of identical lines (" + std::to_string(bad_repeats) + " do not)");
        const LineSpan& last_span = s.spans[s.span_count - 1];
        const int32_t last_shown = last_span.dst_count != last_span.src_count ? last_span.src : last_span.src + last_span.src_count - 1;
        check(lines_identical(*sprite, s.columns, last_shown, want.last, s.cross_first, s.cross_count), at + "it ends with a line identical to line " + std::to_string(want.last) + " (where the original's own strip ends)");
    }
}

// =====================================================================================================================================================
// 3. The composed screens against the mock-ups, pixel for pixel
// =====================================================================================================================================================

constexpr const char* kMapsDir = ORIGINAL_ASSETS_DIR "/Maps";

struct RendererRig {
    RendererRig(const assets::AssetArchive& archive, int32_t w, int32_t h) : width(w), height(h) {
        const QuietStdout quiet;
        ensure_sdl();
        win = SDL_CreateWindow("wide-setup", 0, 0, w, h, SDL_WINDOW_HIDDEN);
        if (win == nullptr || !renderer.init(win, archive)) return;
        renderer.set_canvas_size(w, h);
        ok = true;
    }
    ~RendererRig() {
        renderer.shutdown();
        if (win != nullptr) SDL_DestroyWindow(win);
    }
    RendererRig(const RendererRig&) = delete;
    RendererRig& operator=(const RendererRig&) = delete;

    std::vector<uint8_t> read() {
        std::vector<uint8_t> px(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 0);
        SDL_RenderReadPixels(renderer.get_sdl_renderer(), nullptr, SDL_PIXELFORMAT_RGBA32, px.data(), width * 4);
        return px;
    }

    int32_t width;
    int32_t height;
    SDL_Window* win{nullptr};
    Renderer renderer;
    bool ok{false};
};

/// The seats of the mock-ups' network screens: the leader's (Ana, Ben, a bot) and the guest Ben's (Ben himself first, then Ana, the bot)
MapSelectScreen::RoomView leader_view() {
    MapSelectScreen::RoomView room;
    room.networked = true;
    room.is_host = false;
    room.leader = true;
    room.my_seat = 0;
    room.seats[0] = {true, "Ana", MapSelectScreen::Thumb::Good};
    room.seats[1] = {true, "Ben", MapSelectScreen::Thumb::Ok};
    room.seats[2] = {true, "Bot (Medium)", MapSelectScreen::Thumb::Good};
    room.map_file = "ISLANDS.LVL";
    room.status = "2 players here. Empty seats start as Medium bots.";
    return room;
}

MapSelectScreen::RoomView guest_view() {
    MapSelectScreen::RoomView room = leader_view();
    room.leader = false;
    room.my_seat = 1;
    room.seats[0] = {true, "Ana", MapSelectScreen::Thumb::Ok};
    room.seats[1] = {true, "Ben", MapSelectScreen::Thumb::Good};
    room.status = "Waiting for the leader to start.";
    return room;
}

MapSelectScreen::ChatPanel sample_chat(bool visible) {
    MapSelectScreen::ChatPanel panel;
    panel.visible = visible;
    panel.lines = {{"Ben joined.", true}, {"Ben: hi, which map?", false}, {"Ana: TINY first, then ISLANDS", false}, {"Ben: ok, ready when you are", false}};
    panel.typed = "ready in a minute";
    panel.caret = true;
    return panel;
}

/// A screen of the wide kind in a given state, 1660 ms after it was made (past the refresh; the portraits' loop is at its start: frame 0, which the mock-ups draw)
struct ScreenState {
    MapSelectScreen screen;
    explicit ScreenState(int map_index = 0, bool wide = true) {
        screen.init(kMapsDir);
        screen.set_wide_layout(wide);
        screen.set_player_name("Player");
        screen.set_player_team(0);
        screen.set_selected_index(map_index);
        screen.enter();
        screen.update(1.66f);
    }
    ScreenState(const ScreenState&) = delete;
    ScreenState& operator=(const ScreenState&) = delete;
    void online(bool chat = true) {
        screen.set_room(leader_view());
        screen.set_chat_panel(sample_chat(chat));
        if (chat) screen.set_fill_footer("Empty seats at START:", "Medium bots");
        screen.update(0.0f);
    }
    void guest(bool chat = true) {
        screen.set_room(guest_view());
        screen.set_chat_panel(sample_chat(chat));
    }
};

enum MapIndex { kGauntlet = 0, kIslands = 1, kMedium = 2, kSmall = 3, kTiny = 4, kTreasure = 5 };

/// Pixels of the picture hashed with rectangles blanked out (the text areas: their pixels depend on the machine's font library, the mock-ups' on another): FNV-1a 64 over the RGB bytes,
/// a blanked pixel as (0, 0, 0). The same function made the pinned digests of the mock-ups' PNGs (computed from the mock-ups' own pixels).
uint64_t masked_digest(const std::vector<uint8_t>& rgba, int32_t w, int32_t h, const std::vector<LayoutRect>& masks) {
    Fnv f;
    for (int32_t y = 0; y < h; ++y) {
        for (int32_t x = 0; x < w; ++x) {
            bool masked = false;
            for (const LayoutRect& m : masks) masked = masked || m.contains(x, y);
            const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4u;
            for (size_t k = 0; k < 3; ++k) f.byte(masked ? uint8_t{0} : rgba[i + k]);
        }
    }
    return f.value();
}

uint64_t region_digest(const std::vector<uint8_t>& rgba, int32_t w, const LayoutRect& r) {
    Fnv f;
    for (int32_t y = r.y; y < r.bottom(); ++y) {
        for (int32_t x = r.x; x < r.right(); ++x) {
            const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4u;
            for (size_t k = 0; k < 3; ++k) f.byte(rgba[i + k]);
        }
    }
    return f.value();
}

uint64_t preview_digest(const MapPreview& p) {
    Fnv f;
    for (size_t i = 0; i + 3 < p.rgba.size(); i += 4) {
        f.byte(p.rgba[i]);
        f.byte(p.rgba[i + 1]);
        f.byte(p.rgba[i + 2]);
    }
    return f.value();
}

/// The digest (region_digest's, RGB) of a picture centred in the black box of inner x inner pixels (the box is filled with the black of the original's frames, (7, 11, 15))
uint64_t boxed_digest(const MapPreview& p, int32_t inner) {
    constexpr uint8_t kBoxBlack[3] = {7, 11, 15};
    Fnv f;
    const int32_t ox = (inner - p.width) / 2;
    const int32_t oy = (inner - p.height) / 2;
    for (int32_t y = 0; y < inner; ++y) {
        for (int32_t x = 0; x < inner; ++x) {
            const int32_t px = x - ox;
            const int32_t py = y - oy;
            const bool in = px >= 0 && py >= 0 && px < p.width && py < p.height;
            const size_t i = in ? (static_cast<size_t>(py) * static_cast<size_t>(p.width) + static_cast<size_t>(px)) * 4u : 0;
            for (size_t k = 0; k < 3; ++k) f.byte(in ? p.rgba[i + k] : kBoxBlack[k]);
        }
    }
    return f.value();
}

assets::LevelData load_level(const char* name);

std::vector<LayoutRect> text_masks(bool online_column, bool bot_thumb, bool footer, bool chat) {
    std::vector<LayoutRect> m = {{35, 370, 252, 22}, {35, 438, 366, 22}, {34, 505, 368, 24}};     // the map's name and description, the prompt
    for (int32_t i = 0; i < 4; ++i) m.push_back({733, 93 + 50 * i, 124, 22});                       // the four players' names
    if (bot_thumb) m.push_back({858, 193, 36, 30});                                                  // the thumb of the third row (the mock-up's bot has none)
    m.push_back(online_column ? LayoutRect{375, 340, 256, 22} : LayoutRect{349, 359, 308, 22});   // the caption under the preview
    if (footer) m.push_back({689, 290, 200, 46});
    if (chat) {
        m.push_back({412, 361, 80, 26});                                                             // the label "Chat"
        m.push_back({418, 390, 209, 99});                                                            // the lines
        m.push_back({420, 504, 200, 22});                                                            // the typed text
    }
    return m;
}

struct MockCase {
    const char* name;
    std::function<void(ScreenState&)> setup;
    int map_index;
    SetupVariant variant;
    std::vector<LayoutRect> masks;
    LayoutRect preview;                 // the preview's inner box (the black box's inner square that the picture is centred in)
    uint64_t mock_masked;               // the digest of the mock-up PNG with the masks blanked (the preview's inner box is one of them)
};

void test_mockups(const assets::AssetArchive& arc) {
    group("mockups", "the composed screens are the mock-ups' PNGs, pixel for pixel (text areas masked; the preview separately)");
    RendererRig rig(arc, 960, 540);
    check(rig.ok, "the renderer is up on a 960 x 540 canvas");
    if (!rig.ok) return;
    // The mock-ups drew the map preview as the minimap-colour picture. The preview is now the game's own render of the map (map_preview.hpp), so the mock-ups' own pixels of the preview are
    // no longer the picture (the preview of the real renderer is pinned in test_map_preview); what stays the mock-ups' is everything around it. The mask is the preview's whole inner box
    // (the 300 x 300 or 248 x 248 square: the pictures fill it now, the old ones were smaller whole-scale squares inside it), the pinned digests are those of the mock-ups' PNGs with that
    // mask (the three single-player cases share one: the art outside the preview and the text is the same on every map).
    const LayoutRect single_box{353, 50, 300, 300};
    const LayoutRect online_box{379, 83, 248, 248};
    const std::vector<MockCase> cases = {
        {"C_single_TINY", [](ScreenState&) {}, kTiny, SetupVariant::Single, [&] { auto m = text_masks(false, false, false, false); m.push_back(single_box); return m; }(), single_box, 0xe1806faf8326f5b7ull},
        {"C_single_GAUNTLET", [](ScreenState&) {}, kGauntlet, SetupVariant::Single, [&] { auto m = text_masks(false, false, false, false); m.push_back(single_box); return m; }(), single_box, 0xe1806faf8326f5b7ull},
        {"C_single_ISLANDS", [](ScreenState&) {}, kIslands, SetupVariant::Single, [&] { auto m = text_masks(false, false, false, false); m.push_back(single_box); return m; }(), single_box, 0xe1806faf8326f5b7ull},
        {"C2_online_leader", [](ScreenState& s) { s.online(true); }, kIslands, SetupVariant::Online, [&] { auto m = text_masks(true, true, true, true); m.push_back(online_box); return m; }(), online_box, 0xe80f507abc56da2cull},
        {"C2_online_guest", [](ScreenState& s) { s.guest(true); }, kIslands, SetupVariant::Guest, [&] { auto m = text_masks(true, true, false, true); m.push_back(online_box); return m; }(), online_box, 0x40aab98264b6217full},
    };
    for (const MockCase& c : cases) {
        ScreenState state(c.map_index);
        c.setup(state);
        check(state.screen.setup_variant() == c.variant, std::string(c.name) + ": the screen is the " + (c.variant == SetupVariant::Single ? "single" : c.variant == SetupVariant::Online ? "online host" : "guest") + " variant");
        rig.renderer.begin_frame();
        state.screen.render(rig.renderer, arc);
        const std::vector<uint8_t> px = rig.read();
        const uint64_t got = masked_digest(px, 960, 540, c.masks);
        check(got == c.mock_masked, std::string(c.name) + ": the picture outside the text areas and the preview is the mock-up's: digest " + hex64(got) + ", the mock-up's is " + hex64(c.mock_masked));
        // the preview box shows the game's own render of the map, centred, on black: the screen's pixels are the preview that render_map_preview_world makes for the same renderer
        const std::vector<std::string> files = {"GAUNTLET.LVL", "ISLANDS.LVL", "MEDIUM.LVL", "SMALL.LVL", "TINY.LVL", "TREASURE.LVL"};
        const assets::LevelData level = load_level(files[static_cast<size_t>(c.map_index)].c_str());
        const MapPreview want = render_map_preview_world(rig.renderer, level, c.preview.w);
        check(want.valid() && want.rendered, std::string(c.name) + ": the renderer makes the preview from its own drawing");
        check(region_digest(px, 960, c.preview) == boxed_digest(want, c.preview.w), std::string(c.name) + ": the preview box shows that picture, centred on black: " + hex64(region_digest(px, 960, c.preview)) + ", wanted " + hex64(boxed_digest(want, c.preview.w)));
    }
    // a mask that hides nothing would make every digest above a lie: the same picture with one pixel of the art changed has another digest, and a blanked pixel does not
    {
        ScreenState state(kTiny);
        rig.renderer.begin_frame();
        state.screen.render(rig.renderer, arc);
        std::vector<uint8_t> px = rig.read();
        const std::vector<LayoutRect> masks = text_masks(false, false, false, false);
        const uint64_t a = masked_digest(px, 960, 540, masks);
        px[(static_cast<size_t>(300) * 960 + 120) * 4u] ^= 0x10;                       // inside the art (the map list's box)
        const uint64_t b = masked_digest(px, 960, 540, masks);
        px[(static_cast<size_t>(300) * 960 + 120) * 4u] ^= 0x10;
        px[(static_cast<size_t>(380) * 960 + 100) * 4u] ^= 0x10;                       // inside the map name's text area
        const uint64_t c = masked_digest(px, 960, 540, masks);
        check(a != b && a == c, "the digest sees a pixel of the art and does not see one in a text area");
    }
}

// =====================================================================================================================================================
// 4. The map preview
// =====================================================================================================================================================

assets::LevelData load_level(const char* name) {
    assets::LevelData level;
    level.load_from_file(std::string(kMapsDir) + "/" + name);
    return level;
}

/// A big level made of the cells of GAUNTLET (a map of a community's size): `cols` x `rows` cells, no plants (so that the picture is the cells' colours alone)
constexpr uint64_t kBigSampledDigest = 0x8dc913ccdfd4cf5dull;      // pinned below (printed by --print)

assets::LevelData big_level(uint32_t cols, uint32_t rows) {
    const assets::LevelData base = load_level("GAUNTLET.LVL");
    assets::LevelData big;
    big.version = base.version;
    big.game_mode = base.game_mode;
    big.default_minutes = base.default_minutes;
    big.description = "a big synthetic map";
    big.tile_type_count = base.tile_type_count;
    big.tile_dictionary = base.tile_dictionary;
    big.width.val = cols;
    big.height.val = rows;
    big.layer1_terrain.assign(static_cast<size_t>(cols) * rows, assets::MapCell{});
    big.layer2_interactive.assign(static_cast<size_t>(cols) * rows, assets::MapCell{});
    for (uint32_t y = 0; y < rows; ++y) {
        for (uint32_t x = 0; x < cols; ++x) {
            big.layer1_terrain[static_cast<size_t>(y) * cols + x] = base.get_cell_layer1(x % base.width(), y % base.height());
            assets::MapCell c2 = base.get_cell_layer2(x % base.width(), y % base.height());
            big.layer2_interactive[static_cast<size_t>(y) * cols + x] = c2;
        }
    }
    return big;
}

void test_preview(const assets::AssetArchive& arc) {
    group("preview", "the minimap-colour preview (the fallback of the rendered one): the size rule, the mock-ups' pictures, a big map sampled, plants, the caption");
    const auto& pal = arc.get_palette();
    // the size rule
    struct SizeCase {
        uint32_t cols, rows;
        int32_t inner;
        MapPreviewSize want;
    };
    const SizeCase sizes[] = {
        {60, 60, 300, {300, 300, 5, false}},       {31, 31, 300, {279, 279, 9, false}},    {60, 60, 248, {240, 240, 4, false}},   {124, 100, 248, {248, 200, 2, false}},
        {125, 100, 248, {248, 198, 0, true}},      {250, 200, 248, {248, 198, 0, true}},   {256, 256, 248, {248, 248, 0, true}},  {1, 1, 300, {300, 300, 300, false}},
        {200, 20, 300, {300, 30, 0, true}},        {150, 150, 300, {300, 300, 2, false}},  {151, 80, 300, {300, 158, 0, true}},  {16, 90, 300, {48, 270, 3, false}},
        {0, 5, 300, {0, 0, 0, false}},             {60, 60, 0, {0, 0, 0, false}},          {5, 0, 300, {0, 0, 0, false}},
    };
    for (const SizeCase& c : sizes) {
        const MapPreviewSize got = map_preview_size(c.cols, c.rows, c.inner);
        check(got == c.want, "a " + std::to_string(c.cols) + " x " + std::to_string(c.rows) + " map in a box of " + std::to_string(c.inner) + ": " + std::to_string(got.width) + " x " + std::to_string(got.height) + " scale " +
                                 std::to_string(got.scale) + (got.sampled ? " sampled" : "") + ", wanted " + std::to_string(c.want.width) + " x " + std::to_string(c.want.height) + " scale " + std::to_string(c.want.scale) + (c.want.sampled ? " sampled" : ""));
    }
    // the mock-ups' pictures: TINY at 9 pixels per cell, GAUNTLET and ISLANDS at 5 (300 box), ISLANDS at 4 (248 box); the digests are those of the mock-ups' own pixels
    struct PictureCase {
        const char* map;
        int32_t inner;
        int32_t scale;
        uint64_t digest;
        uint32_t players;
    };
    const PictureCase pictures[] = {
        {"TINY.LVL", 300, 9, 0xa69b975a94620fd0ull, 4},
        {"GAUNTLET.LVL", 300, 5, 0x4736f4e8163df59dull, 4},
        {"ISLANDS.LVL", 300, 5, 0x39589fd86c7d7fd9ull, 4},
        {"ISLANDS.LVL", 248, 4, 0xbb4f27b34ea6e8e5ull, 4},
    };
    for (const PictureCase& c : pictures) {
        const assets::LevelData level = load_level(c.map);
        const MapPreview p = render_map_preview_in_box(level, pal, c.inner);
        const std::string at = std::string(c.map) + " in " + std::to_string(c.inner) + ": ";
        check(p.valid() && p.scale == c.scale && !p.sampled && p.width == static_cast<int32_t>(level.width()) * c.scale && p.height == static_cast<int32_t>(level.height()) * c.scale, at + "a whole scale of " + std::to_string(c.scale));
        check(preview_digest(p) == c.digest, at + "the picture is the mock-up's: " + hex64(preview_digest(p)) + ", wanted " + hex64(c.digest));
        check(p.cols == level.width() && p.rows == level.height() && p.players == c.players, at + "its size and its players");
        const MapPreview same = render_map_preview(level, pal, c.scale);
        check(preview_digest(same) == preview_digest(p), at + "render_map_preview at the scale gives the same picture");
    }
    // the picture shows the colours of the minimap's table: water, the four hills in their team colours, the grass; nothing is made up
    {
        const assets::LevelData islands = load_level("ISLANDS.LVL");
        const MapPreview p = render_map_preview(islands, pal, 1);
        std::map<uint32_t, int> colours;
        for (size_t i = 0; i + 3 < p.rgba.size(); i += 4) ++colours[(static_cast<uint32_t>(p.rgba[i]) << 16) | (static_cast<uint32_t>(p.rgba[i + 1]) << 8) | p.rgba[i + 2]];
        auto has = [&](uint8_t index) { return colours.count((static_cast<uint32_t>(pal[index].r) << 16) | (static_cast<uint32_t>(pal[index].g) << 8) | pal[index].b) > 0; };
        check(has(37), "water has the minimap's water colour (37)");
        check(has(47) && has(158) && has(211) && has(239), "the four hills are in their team colours: green 47, red 158, blue 211, black 239");
        check(has(171), "food and power-ups have the minimap's food colour (171)");
        check(has(251) || has(235), "the ground has the minimap's gravel / slate colour");
        // a hill is a square of cells in its colour: the number of green pixels is a whole number of hill squares
        int green = 0;
        for (size_t i = 0; i + 3 < p.rgba.size(); i += 4) green += (p.rgba[i] == pal[47].r && p.rgba[i + 1] == pal[47].g && p.rgba[i + 2] == pal[47].b) ? 1 : 0;
        check(green > 0 && green % 4 == 0, "the green hill covers whole cells");
    }
    // a big map (a community map's size): sampled, nearest neighbour; every pixel is the cell it falls on, found by an oracle that does not share the code (the picture at one pixel per cell)
    {
        const assets::LevelData big = big_level(250, 200);
        check(big.width() == 250 && big.height() == 200, "the synthetic level is 250 x 200 cells");
        const MapPreview one = render_map_preview(big, pal, 1);
        check(one.valid() && one.width == 250 && one.height == 200, "at one pixel per cell it is 250 x 200");
        const MapPreview box = render_map_preview_in_box(big, pal, 248);
        check(box.valid() && box.sampled && box.scale == 0 && box.width == 248 && box.height == 198, "in a box of 248 it is sampled to 248 x 198 (the longer side fills the box)");
        int wrong = 0;
        for (int32_t y = 0; y < box.height; ++y) {
            for (int32_t x = 0; x < box.width; ++x) {
                const int64_t cx = static_cast<int64_t>(x) * 250 / box.width;
                const int64_t cy = static_cast<int64_t>(y) * 200 / box.height;
                const size_t a = (static_cast<size_t>(y) * static_cast<size_t>(box.width) + static_cast<size_t>(x)) * 4u;
                const size_t b = (static_cast<size_t>(cy) * 250u + static_cast<size_t>(cx)) * 4u;
                wrong += (box.rgba[a] != one.rgba[b] || box.rgba[a + 1] != one.rgba[b + 1] || box.rgba[a + 2] != one.rgba[b + 2]) ? 1 : 0;
            }
        }
        check(wrong == 0, "every pixel of the sampled picture is the colour of the cell under it (" + std::to_string(wrong) + " are not)");
        const MapPreview doubled = render_map_preview(big, pal, 2);
        int wrong2 = 0;
        for (int32_t y = 0; y < doubled.height; ++y) {
            for (int32_t x = 0; x < doubled.width; ++x) {
                const size_t a = (static_cast<size_t>(y) * static_cast<size_t>(doubled.width) + static_cast<size_t>(x)) * 4u;
                const size_t b = (static_cast<size_t>(y / 2) * 250u + static_cast<size_t>(x / 2)) * 4u;
                wrong2 += (doubled.rgba[a] != one.rgba[b] || doubled.rgba[a + 1] != one.rgba[b + 1] || doubled.rgba[a + 2] != one.rgba[b + 2]) ? 1 : 0;
            }
        }
        check(doubled.width == 500 && wrong2 == 0, "a whole scale of 2 shows every cell as a 2 x 2 square, nothing else");
        const uint64_t digest = preview_digest(box);
        if (g_print) std::printf("    PINNED big_level_250x200_in_248 %s\n", hex64(digest).c_str());
        check(digest == kBigSampledDigest, "the sampled picture of the 250 x 200 level is the pinned one: " + hex64(digest) + ", wanted " + hex64(kBigSampledDigest));
    }
    // the plants' dots: a plant of the original's table with a size flag draws a square of that many cells on its cell (colour of the table), none without one. The oracle paints them
    // on the picture of the same level without its plants, in the mock-up's own formula (x * s - s * (flag - 1) / 2, s * flag pixels wide)
    {
        const assets::LevelData gauntlet = load_level("GAUNTLET.LVL");
        sim::Grid grid;
        grid.init_from_level(gauntlet);
        check(!grid.plants().empty(), "GAUNTLET has plants (the dots are made of them)");
        assets::LevelData bare = gauntlet;
        bare.anthill_spawns.clear();                                          // the plants are the records of Block 1 with the property bits of a flower or a clover
        sim::Grid bare_grid;
        bare_grid.init_from_level(bare);
        check(bare_grid.plants().empty(), "without its Block 1 records the level has no plants");
        const MapPreview with = render_map_preview(gauntlet, pal, 5);
        MapPreview expect = render_map_preview(bare, pal, 5);
        int dots = 0;
        for (const auto& plant : grid.plants()) {
            const int32_t flag = minimap_object_entry(plant.tile_id) >> 8;
            if (flag <= 0) continue;
            ++dots;
            const assets::ColorRGBA c = pal[minimap_object_entry(plant.tile_id) & 0xffu];
            for (int32_t dy = 0; dy < 5 * flag; ++dy) {
                for (int32_t dx = 0; dx < 5 * flag; ++dx) {
                    const int32_t X = plant.x * 5 + dx - (5 * (flag - 1)) / 2;
                    const int32_t Y = plant.y * 5 + dy - (5 * (flag - 1)) / 2;
                    if (X < 0 || Y < 0 || X >= expect.width || Y >= expect.height) continue;
                    uint8_t* d = &expect.rgba[(static_cast<size_t>(Y) * static_cast<size_t>(expect.width) + static_cast<size_t>(X)) * 4u];
                    d[0] = c.r;
                    d[1] = c.g;
                    d[2] = c.b;
                }
            }
        }
        check(dots > 0, "some of GAUNTLET's plants have a size flag: " + std::to_string(dots) + " dots");
        check(with.valid() && expect.valid() && with.rgba == expect.rgba && preview_digest(with) != preview_digest(render_map_preview(bare, pal, 5)), "the dots are the plants' squares on the picture without them, and they are there");
    }
    // the caption
    {
        MapPreview p;
        p.cols = 60;
        p.rows = 60;
        p.players = 4;
        check(map_preview_caption(p) == "60 x 60 cells \xC2\xB7 4 players", "the caption: \"60 x 60 cells \xC2\xB7 4 players\"");
        p.players = 1;
        check(map_preview_caption(p) == "60 x 60 cells \xC2\xB7 1 player", "one player is a \"player\"");
        p.players = 0;
        p.cols = 100;
        p.rows = 81;
        check(map_preview_caption(p) == "100 x 81 cells", "a map without a hill has no player count");
    }
    // an empty request gives an empty picture, a level that cannot be a grid too
    {
        const assets::LevelData gauntlet = load_level("GAUNTLET.LVL");
        check(!render_map_preview(gauntlet, pal, 0).valid() && !render_map_preview(gauntlet, pal, MapPreviewSize{}).valid(), "no scale, no size: no picture");
        const assets::LevelData empty;
        check(!render_map_preview_in_box(empty, pal, 300).valid(), "an empty level has no picture");
    }
}



struct Golden {
    const char* name;
    uint64_t hash;
    uint64_t count;
};

struct Measured {
    std::string name;
    uint64_t hash;
    uint64_t count;
};
std::vector<Measured> g_measured;

void fingerprint(const std::string& name, uint64_t hash, uint64_t count) { g_measured.push_back({name, hash, count}); }

// =====================================================================================================================================================
// 5. What the screen draws where
// =====================================================================================================================================================

/// Records the screen's calls in order (the deterministic measures of a text: 6 pixels a character, the cell height of the font)
class Spy : public IRenderer {
public:
    enum class Kind : uint8_t { Sprite, Region, Named, Fill, Rect, Text, Image, Team, Clip, ClearClip, Origin };
    struct Ev {
        Kind kind{Kind::Sprite};
        std::string name;                     // the sprite's name, the text
        int32_t x{0}, y{0}, w{0}, h{0};
        int32_t sx{0}, sy{0}, sw{0}, sh{0};
        assets::ColorRGBA colour{};
        FontSize size{FontSize::Px12};
        uint64_t image_hash{0};
        const uint8_t* image{nullptr};
    };

    explicit Spy(const assets::AssetArchive& archive) : arc_(archive) {}   // (this renderer draws no offscreen images: the screens it sees show the minimap-colour preview)

    void draw_sprite(uint32_t id, int32_t x, int32_t y, bool) override { push(Kind::Sprite, arc_.get_sprite(id).name, x, y); }
    void draw_named_sprite(const std::string& name, int32_t x, int32_t y, bool) override { push(Kind::Named, name, x, y); }
    void draw_sprite_region(uint32_t id, int32_t x, int32_t y, int32_t w, int32_t h, int32_t sx, int32_t sy, int32_t sw, int32_t sh) override {
        Ev e = base(Kind::Region);
        e.name = arc_.get_sprite(id).name;
        e.x = x; e.y = y; e.w = w; e.h = h; e.sx = sx; e.sy = sy; e.sw = sw; e.sh = sh;
        events.push_back(e);
    }
    void fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, assets::ColorRGBA c) override {
        Ev e = base(Kind::Fill);
        e.x = x; e.y = y; e.w = w; e.h = h; e.colour = c;
        events.push_back(e);
    }
    void draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, assets::ColorRGBA c) override {
        Ev e = base(Kind::Rect);
        e.x = x; e.y = y; e.w = w; e.h = h; e.colour = c;
        events.push_back(e);
    }
    void draw_text(const std::string& s, int32_t x, int32_t y, assets::ColorRGBA c) override { text(s, x, y, c, FontSize::Px12); }
    void draw_text(const std::string& s, int32_t x, int32_t y, assets::ColorRGBA c, FontSize size) override { text(s, x, y, c, size); }
    int32_t get_text_width(const std::string& s, FontSize = FontSize::Px12) const override { return static_cast<int32_t>(s.size()) * 6; }
    int32_t get_text_height(FontSize size = FontSize::Px12) const override { return font_cell_height(size); }
    void set_hud_team(uint8_t team) override {
        Ev e = base(Kind::Team);
        e.x = team;
        events.push_back(e);
    }
    void set_clip_rect(int32_t x, int32_t y, int32_t w, int32_t h) override {
        Ev e = base(Kind::Clip);
        e.x = x; e.y = y; e.w = w; e.h = h;
        events.push_back(e);
    }
    void clear_clip_rect() override { events.push_back(base(Kind::ClearClip)); }
    void set_origin(int32_t x, int32_t y) override {
        Ev e = base(Kind::Origin);
        e.x = x; e.y = y;
        events.push_back(e);
    }
    void draw_rgba_image(int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t* rgba) override {
        Ev e = base(Kind::Image);
        e.x = x; e.y = y; e.w = w; e.h = h;
        e.image = rgba;
        Fnv f;
        for (size_t i = 0; i < static_cast<size_t>(w) * static_cast<size_t>(h) * 4u; ++i) f.byte(rgba[i]);
        e.image_hash = f.value();
        events.push_back(e);
    }

    std::vector<Ev> events;

    size_t count(Kind k) const {
        size_t n = 0;
        for (const Ev& e : events) n += e.kind == k ? 1u : 0u;
        return n;
    }
    /// The text drawn with exactly this string, or null
    const Ev* find_text(const std::string& s) const {
        for (const Ev& e : events) {
            if (e.kind == Kind::Text && e.name == s) return &e;
        }
        return nullptr;
    }
    bool has_sprite_at(const std::string& name, int32_t x, int32_t y) const {
        for (const Ev& e : events) {
            if ((e.kind == Kind::Sprite || e.kind == Kind::Named) && e.name == name && e.x == x && e.y == y) return true;
        }
        return false;
    }
    /// Did anything but the clay and the frame start inside this rectangle? (text, a fill, a region, a sprite, an image)
    bool anything_in(const LayoutRect& r) const {
        for (const Ev& e : events) {
            if (e.kind == Kind::Team || e.kind == Kind::Clip || e.kind == Kind::ClearClip || e.kind == Kind::Origin) continue;
            if (e.kind == Kind::Region && (e.name == "dclay96.bmp")) continue;
            if (r.contains(e.x, e.y)) return true;
        }
        return false;
    }

    uint64_t fingerprint() const {
        Fnv f;
        for (const Ev& e : events) {
            f.byte(static_cast<uint8_t>(e.kind));
            f.text(e.name);
            for (int32_t v : {e.x, e.y, e.w, e.h, e.sx, e.sy, e.sw, e.sh}) f.i32(v);
            f.byte(e.colour.r); f.byte(e.colour.g); f.byte(e.colour.b); f.byte(e.colour.a);
            f.byte(static_cast<uint8_t>(e.size));
            for (int i = 0; i < 8; ++i) f.byte(static_cast<uint8_t>((e.image_hash >> (8 * i)) & 0xffu));
        }
        return f.value();
    }

private:
    Ev base(Kind k) const {
        Ev e;
        e.kind = k;
        return e;
    }
    void push(Kind k, const std::string& name, int32_t x, int32_t y) {
        Ev e = base(k);
        e.name = name;
        e.x = x;
        e.y = y;
        events.push_back(e);
    }
    void text(const std::string& s, int32_t x, int32_t y, assets::ColorRGBA c, FontSize size) {
        Ev e = base(Kind::Text);
        e.name = s;
        e.x = x; e.y = y; e.colour = c; e.size = size;
        events.push_back(e);
    }
    QuietCerr quiet_;
    const assets::AssetArchive& arc_;
};

/// The parts of a frame-0 animation of the original's UI (absolute coordinates), moved: what the screen must draw for a button
std::vector<std::pair<std::string, std::pair<int32_t, int32_t>>> anim_parts(const assets::AssetArchive& arc, const char* name, int32_t dx, int32_t dy) {
    std::vector<std::pair<std::string, std::pair<int32_t, int32_t>>> out;
    const auto* seq = arc.find_animation(name);
    if (seq == nullptr || seq->subitems.empty()) return out;
    for (const auto& part : seq->subitems[0].frames) out.push_back({arc.get_sprite(part.sprite_index).name, {part.dx + dx, part.dy + dy}});
    return out;
}

/// The union of the parts of a frame-0 animation, moved: the rectangle of the picture that shows
LayoutRect anim_rect(const assets::AssetArchive& arc, const char* name, int32_t dx, int32_t dy) {
    int32_t x0 = 1 << 20, y0 = 1 << 20, x1 = -(1 << 20), y1 = -(1 << 20);
    const auto* seq = arc.find_animation(name);
    if (seq == nullptr || seq->subitems.empty()) return LayoutRect{};
    for (const auto& part : seq->subitems[0].frames) {
        const auto& sp = arc.get_sprite(part.sprite_index);
        x0 = std::min(x0, part.dx);
        y0 = std::min(y0, part.dy);
        x1 = std::max(x1, part.dx + static_cast<int32_t>(sp.width));
        y1 = std::max(y1, part.dy + static_cast<int32_t>(sp.height));
    }
    return LayoutRect{x0 + dx, y0 + dy, x1 - x0, y1 - y0};
}

bool drew_all(const Spy& spy, const std::vector<std::pair<std::string, std::pair<int32_t, int32_t>>>& parts) {
    if (parts.empty()) return false;
    for (const auto& p : parts) {
        if (!spy.has_sprite_at(p.first, p.second.first, p.second.second)) return false;
    }
    return true;
}

void test_state(const assets::AssetArchive& arc) {
    group("state", "what the wide screen draws where: the labels, the seats, the buttons' pictures, the chat column and the footer (only when asked), the \"No preview\" box");
    // before the refresh (500 ms after the screen was made) the labels, the portraits and the preview are not there; the art and the buttons are
    {
        MapSelectScreen screen;
        screen.init(kMapsDir);
        screen.set_wide_layout(true);
        Spy spy(arc);
        screen.render(spy, arc);
        check(spy.count(Spy::Kind::Text) == 0 && spy.count(Spy::Kind::Image) == 0, "before the refresh there is no text and no preview");
        check(drew_all(spy, anim_parts(arc, "leave1", 320, 0)) && drew_all(spy, anim_parts(arc, "start1", 320, 60)) && drew_all(spy, anim_parts(arc, "up1", 70, 60)), "the buttons are there: Leave Game, START!, Up (moved with the layout)");
        check(spy.count(Spy::Kind::Region) > 60, "the clay, the frame and the strips are drawn as parts of the original's pieces");
    }
    // a game on this machine
    {
        ScreenState state(kTiny);
        Spy spy(arc);
        state.screen.render(spy, arc);
        const SetupLayout& l = SetupLayout::of(SetupVariant::Single);
        const Spy::Ev* name = spy.find_text("TINY");
        const Spy::Ev* info = spy.find_text("Tiny map with no PowerUps (6 min)");
        const Spy::Ev* prompt = spy.find_text("Press START when all players' thumbs have appeared.");
        check(name != nullptr && name->x == l.name_text.x && name->y == l.name_text.y && name->size == FontSize::Px18 && name->colour.r == 239 && name->colour.g == 231 && name->colour.b == 223, "the map's name is a label at (36, 372), 18 px, in the labels' cream");
        check(info != nullptr && info->x == l.info_text.x && info->y == l.info_text.y && info->size == FontSize::Px18, "its description (and minutes) at (36, 440)");
        check(prompt != nullptr && prompt->x == l.prompt_text.x && prompt->y == l.prompt_text.y && prompt->size == FontSize::Px14, "the prompt at (36, 507), 14 px");
        const Spy::Ev* player = spy.find_text("Player");
        check(player != nullptr && player->x == 735 && player->y == 95 && player->size == FontSize::Px18, "the player's name at (735, 95)");
        check(spy.has_sprite_at("thumb1.bmp", 860, 95), "the player's thumb at (860, 95)");
        // the portrait: the animation's parts at the origin (715, 115), in the seat's colour
        const auto* stand = arc.find_animation("agst301");
        bool portrait = stand != nullptr;
        if (stand != nullptr) {
            const size_t frame = Renderer::get_anim_subitem_by_time(*stand, 1660);
            for (const auto& part : stand->subitems[frame].frames) portrait = portrait && spy.has_sprite_at(arc.get_sprite(part.sprite_index).name, 715 + part.dx, 115 + part.dy);
        }
        check(portrait, "the portrait's parts around the origin (715, 115)");
        // the preview: the picture at its place and its caption under it
        const Spy::Ev* image = nullptr;
        for (const Spy::Ev& e : spy.events) {
            if (e.kind == Spy::Kind::Image) image = &e;
        }
        check(image != nullptr && image->x == 363 && image->y == 60 && image->w == 279 && image->h == 279, "the preview picture of TINY: 279 square at (363, 60)");
        const std::string caption = "31 x 31 cells \xC2\xB7 4 players";
        const Spy::Ev* cap = spy.find_text(caption);
        check(cap != nullptr && cap->y == l.caption_y && cap->x == 503 - spy.get_text_width(caption) / 2 && cap->size == FontSize::Px18 && cap->colour.r == 239, "its caption under the box, centred on 503, in the players' names' font and colour");
        check(spy.find_text("Chat") == nullptr && !spy.anything_in(LayoutRect{414, 363, 217, 148}), "no chat column on a game of one machine");
        check(spy.find_text("Empty seats at START:") == nullptr, "no bot-fill footer on a game of one machine");
        // the buttons' pictures follow the layout's rectangles: the union of the parts of the animation, moved, is the layout's rectangle
        check_rect(anim_rect(arc, "leave1", 320, 0), l.leave, "Leave Game's picture is the layout's rectangle");
        check_rect(anim_rect(arc, "up1", 70, 60), l.up, "Up's picture");
        check_rect(anim_rect(arc, "down1", 70, 60), l.down, "Down's picture");
        auto united = [&](const char* a, const char* b) {
            const LayoutRect ra = anim_rect(arc, a, 320, 60), rb = anim_rect(arc, b, 320, 60);
            const int32_t x0 = std::min(ra.x, rb.x), y0 = std::min(ra.y, rb.y);
            return LayoutRect{x0, y0, std::max(ra.right(), rb.right()) - x0, std::max(ra.bottom(), rb.bottom()) - y0};
        };
        check_rect(united("d_on1", "d_on3"), l.fog_on, "Fog On's rectangle is the union of its first and third picture (docs 5.52)");
        check_rect(united("d_off1", "d_off3"), l.fog_off, "Fog Off's rectangle is the union of its first and third picture");
        check_rect(anim_rect(arc, "start1", 320, 60), l.start, "START!'s picture");
        // the fog buttons: Off is down (d_off3) while the fog is off, On is up
        check(drew_all(spy, anim_parts(arc, "d_off3", 320, 60)) && drew_all(spy, anim_parts(arc, "d_on1", 320, 60)), "Fog Off is shown down, Fog On up");
    }
    // the buttons' hover and pressed pictures
    {
        ScreenState state(kTiny);
        MapSelectScreen& s = state.screen;
        s.handle_mouse_motion(890, 512);            // over START!
        Spy hover(arc);
        s.render(hover, arc);
        check(drew_all(hover, anim_parts(arc, "start2", 320, 60)) && !drew_all(hover, anim_parts(arc, "start1", 320, 60)), "the pointer over START!: its hover picture");
        s.handle_mouse_down(890, 512, SDL_BUTTON_LEFT);
        Spy down(arc);
        s.render(down, arc);
        check(drew_all(down, anim_parts(arc, "start3", 320, 60)), "pressed: its pressed picture");
        s.handle_mouse_up(0, 0, SDL_BUTTON_LEFT);
        s.handle_mouse_motion(318, 371);            // over Up
        s.handle_mouse_down(318, 371, SDL_BUTTON_LEFT);
        Spy up(arc);
        s.render(up, arc);
        check(drew_all(up, anim_parts(arc, "up3", 70, 60)), "pressed Up: its pressed picture, at the map list's buttons' place");
        s.handle_mouse_up(318, 371, SDL_BUTTON_LEFT);
        s.handle_mouse_motion(867, 444);            // over Fog On
        s.handle_mouse_down(867, 444, SDL_BUTTON_LEFT);
        s.handle_mouse_up(867, 444, SDL_BUTTON_LEFT);
        check(s.is_fog_of_war_enabled(), "a click on Fog On (842 .. 891 x 432 .. 456) turns the fog on");
        Spy fog(arc);
        s.render(fog, arc);
        check(drew_all(fog, anim_parts(arc, "d_on3", 320, 60)) && drew_all(fog, anim_parts(arc, "d_off1", 320, 60)), "Fog On is shown down, Fog Off up");
    }
    // the network screens
    {
        ScreenState state(kIslands);
        state.online(false);                       // the leader's screen, the chat column NOT asked for
        const SetupLayout& l = SetupLayout::of(SetupVariant::Online);
        Spy spy(arc);
        state.screen.render(spy, arc);
        check(spy.find_text("Ana") != nullptr && spy.find_text("Ana")->x == 735 && spy.find_text("Ana")->y == 95, "the leader's seats: Ana in the first row");
        check(spy.find_text("Ben") != nullptr && spy.find_text("Ben")->y == 145 && spy.find_text("Bot (Medium)") != nullptr && spy.find_text("Bot (Medium)")->y == 195, "Ben in the second, the bot in the third");
        check(spy.has_sprite_at("thumb1.bmp", 860, 95) && spy.has_sprite_at("thumb2.bmp", 860, 145), "the thumbs: the leader's own is good, Ben's is the second (ok)");
        check(spy.find_text("ISLANDS") != nullptr && spy.find_text("Island hopping, expert map (12 min)") != nullptr, "the room's map is named and described");
        check(spy.find_text("2 players here. Empty seats start as Medium bots.") != nullptr, "the room's status is the prompt");
        const Spy::Ev* cap = spy.find_text("60 x 60 cells \xC2\xB7 4 players");
        check(cap != nullptr && cap->y == 341, "the caption under the 248 box, at y = 341");
        check(spy.find_text("Chat") == nullptr && spy.find_text("Empty seats at START:") == nullptr, "the chat column and the footer are RESERVED: nothing is drawn there until they are asked for");
        check(!spy.anything_in(LayoutRect{l.chat.box.x - 2, l.chat.label_y - 2, l.chat.box.w + 4, 148}), "... not the label, not the black box, not the input box (the clay shows)");
        check(!spy.anything_in(LayoutRect{l.footer_x - 4, l.footer_y1 - 2, 190, 44}), "... and nothing in the Players' Status box's foot");
    }
    {
        ScreenState state(kIslands);
        state.online(true);                        // asked for: the frame, the label, the lines, the input, the footer
        const SetupLayout& l = SetupLayout::of(SetupVariant::Online);
        Spy spy(arc);
        state.screen.render(spy, arc);
        const Spy::Ev* label = spy.find_text("Chat");
        bool shadow = false, teal = false;
        for (const Spy::Ev& e : spy.events) {
            if (e.kind != Spy::Kind::Text || e.name != "Chat") continue;
            shadow = shadow || (e.x == 415 && e.y == 364 && e.colour.r == 7 && e.colour.g == 11 && e.colour.b == 15 && e.size == FontSize::Px20);
            teal = teal || (e.x == 414 && e.y == 363 && e.colour.r == 59 && e.colour.g == 151 && e.colour.b == 111 && e.size == FontSize::Px20);
        }
        check(label != nullptr && shadow && teal, "the label \"Chat\": TrueType 20 px, the engraved labels' teal at (414, 363) over their ink shadow one pixel down and right");
        // the black box: the efram construction at the chat box (a flat fill inside, the corners, the strips)
        bool fill = false, c1 = false, c4 = false;
        for (const Spy::Ev& e : spy.events) {
            fill = fill || (e.kind == Spy::Kind::Fill && e.x == 418 && e.y == 390 && e.w == 209 && e.h == 99 && e.colour.r == 7 && e.colour.g == 11 && e.colour.b == 15);
            c1 = c1 || (e.kind == Spy::Kind::Named && e.name == "efram1c.bmp" && e.x == 414 && e.y == 386);
            c4 = c4 || (e.kind == Spy::Kind::Named && e.name == "efram4c.bmp" && e.x == 414 + 217 - 32 && e.y == 386 + 4 + 99);
        }
        check(fill && c1 && c4, "the chat box is a black efram box: the flat fill (418, 390, 209 x 99) and its corners, as the Players' Status box is");
        // the input box: statline's rows 11 .. 40 as runs of columns at (414, 500)
        int32_t input_width = 0;
        bool input_rows = true;
        for (const Spy::Ev& e : spy.events) {
            if (e.kind == Spy::Kind::Region && e.name == "statline.bmp" && e.y == 500) {
                input_width += e.w;
                input_rows = input_rows && e.h == 30 && e.sy == 11 && e.sh == 30;
            }
        }
        check(input_width == 217 && input_rows, "the input box is statline's box (rows 11 .. 40) 217 columns wide at y = 500");
        // the lines: the newest at the bottom, in the names' font and colour; a notice smaller; the area is 209 x 99 starting at (418, 390): its bottom row is 488
        struct Line {
            const char* text;
            int32_t y;
            FontSize size;
        };
        const Line lines[] = {{"Ben joined.", 420, FontSize::Px14}, {"Ben: hi, which map?", 434, FontSize::Px18}, {"Ana: TINY first, then ISLANDS", 452, FontSize::Px18}, {"Ben: ok, ready when you are", 470, FontSize::Px18}};
        for (const Line& ln : lines) {
            const Spy::Ev* e = spy.find_text(ln.text);
            check(e != nullptr && e->x == 422 && e->y == ln.y && e->size == ln.size && e->colour.r == 239 && e->colour.g == 231 && e->colour.b == 223, std::string("the chat line \"") + ln.text + "\" at (422, " + std::to_string(ln.y) + ")");
        }
        const Spy::Ev* typed = spy.find_text("ready in a minute");
        check(typed != nullptr && typed->x == 424 && typed->y == 508 && typed->size == FontSize::Px18 && typed->colour.r == 239, "the typed text in the input box at (424, 508), 18 px, in the names' colour");
        check(spy.find_text("_") != nullptr && spy.find_text("_")->x > 424, "the caret (an underscore, as every edit field of the original) follows it");
        // the footer
        const Spy::Ev* f1 = spy.find_text("Empty seats at START:");
        const Spy::Ev* f2 = spy.find_text("Medium bots");
        check(f1 != nullptr && f1->x == l.footer_x && f1->y == l.footer_y1 && f1->size == FontSize::Px18 && f1->colour.r == 239, "the footer's first line at (691, 294), 18 px, in the names' colour");
        check(f2 != nullptr && f2->x == l.footer_x && f2->y == l.footer_y2 && f2->size == FontSize::Px18, "its second line at (691, 314)");
    }
    {
        // the chat panel's lines wrap and the oldest drop off the top: many long lines, only what fits (bottom row 488) stays
        ScreenState state(kIslands);
        state.online(true);
        MapSelectScreen::ChatPanel panel = sample_chat(true);
        panel.lines.clear();
        for (int i = 0; i < 12; ++i) panel.lines.push_back({"line " + std::to_string(i), false});
        panel.lines.push_back({"a very long message that cannot fit on one line of the chat box at all", false});
        panel.typed.clear();
        panel.caret = false;
        state.screen.set_chat_panel(panel);
        Spy spy(arc);
        state.screen.render(spy, arc);
        // the Spy measures 6 pixels a character: the wrap width is 209 - 8 = 201 -> 33 characters a line: the long message is three lines, so 5 rows of 18 px fit in 97 pixels and the last row ends at 488
        check(spy.find_text("line 11") != nullptr && spy.find_text("line 0") == nullptr && spy.find_text("line 8") == nullptr, "the newest lines are shown, the oldest are dropped");
        int in_box = 0;
        bool ordered = true;
        int32_t last_y = -1;
        for (const Spy::Ev& e : spy.events) {
            if (e.kind == Spy::Kind::Text && e.x == 422 && e.size == FontSize::Px18) {
                ++in_box;
                ordered = ordered && e.y > last_y && e.y >= 390 && e.y + 18 <= 390 + 99;
                last_y = e.y;
            }
        }
        check(in_box == 5 && ordered, "five 18 px rows stand inside the box, in order (" + std::to_string(in_box) + ")");
        check(spy.find_text("_") == nullptr, "no caret without the flag");
    }
    // the guest
    {
        ScreenState state(kIslands);
        state.guest(true);
        state.screen.set_fill_footer("Empty seats at START:", "Medium bots");      // a guest has no such choice: nothing is drawn
        Spy spy(arc);
        state.screen.render(spy, arc);
        check(state.screen.setup_variant() == SetupVariant::Guest, "the screen is the guest's");
        check(spy.find_text("Empty seats at START:") == nullptr, "a guest has no bot-fill footer");
        check(spy.find_text("Chat") != nullptr && spy.find_text("ready in a minute") != nullptr, "a guest's chat column is there when asked for");
        check(spy.find_text("Ben") != nullptr && spy.find_text("Ben")->y == 95 && spy.find_text("Ana")->y == 145 && spy.has_sprite_at("thumb1.bmp", 860, 95) && spy.has_sprite_at("thumb2.bmp", 860, 145), "the guest sees itself first, then Ana (whose thumb is ok)");
        check(drew_all(spy, anim_parts(arc, "leave1", 320, 0)) && !drew_all(spy, anim_parts(arc, "start1", 320, 60)) && !drew_all(spy, anim_parts(arc, "up1", 70, 60)), "only Leave Game is a button on the guest's screen");
        MapSelectScreen::RoomView room = guest_view();
        state.screen.set_room(room);
        state.screen.follow_host_choice("ISLANDS.LVL", true);
        Spy fog(arc);
        state.screen.render(fog, arc);
        check(drew_all(fog, anim_parts(arc, "d_fowyes", 320, 60)), "the host's Fog of War choice is shown on the fixed box: \"Yes\"");
    }
    // a map name that is longer than its label's box is cut at the box, in every place that shows it (the original draws into a surface of the label's size: what does not fit is not seen).
    // Spy::get_text_width is 6 pixels a character, so the 249-pixel box holds 41 characters; no text mask is involved: it is the text that the screen asked the renderer to draw
    {
        const std::string long_stem = "A_VERY_LONG_COMMUNITY_MAP_NAME_THAT_DOES_NOT_FIT_ITS_LABEL_AT_ALL_AND_GOES_ON";
        const SetupLayout& l = SetupLayout::of(SetupVariant::Single);
        check(static_cast<int32_t>(long_stem.size()) * 6 > l.name_text.w + 120, "(setup) the test's map name is far wider than the name label's box");
        auto name_drawn = [&](const Spy& spy, const std::string& stem, int32_t x, int32_t y) -> std::string {
            for (const Spy::Ev& e : spy.events) {
                if (e.kind == Spy::Kind::Text && e.x == x && e.y == y && e.size == FontSize::Px18 && !e.name.empty() && stem.compare(0, e.name.size(), e.name) == 0) return e.name;
            }
            return "";
        };
        // a game on this machine: the map that the list has selected
        const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("ants_wide_setup_longname_" + std::to_string(static_cast<unsigned long long>(SDL_GetPerformanceCounter())));
        std::filesystem::create_directories(dir);
        std::error_code ignore;
        std::filesystem::copy_file(std::string(kMapsDir) + "/TINY.LVL", dir / (long_stem + ".LVL"), std::filesystem::copy_options::overwrite_existing, ignore);
        {
            MapSelectScreen screen;
            screen.init(dir.string());
            screen.set_wide_layout(true);
            screen.set_player_name("Player");
            screen.set_player_team(0);
            screen.enter();
            screen.update(1.66f);
            Spy spy(arc);
            screen.render(spy, arc);
            const std::string drawn = name_drawn(spy, long_stem, l.name_text.x, l.name_text.y);
            check(screen.get_maps().size() == 1 && !drawn.empty() && drawn.size() < long_stem.size() && spy.get_text_width(drawn) <= l.name_text.w && spy.get_text_width(drawn) > l.name_text.w - 12,
                  "a long map name is cut at the name label's box: " + std::to_string(drawn.size()) + " of " + std::to_string(long_stem.size()) + " characters, " + std::to_string(spy.get_text_width(drawn)) + " of " + std::to_string(l.name_text.w) + " pixels");
        }
        // a guest: the room's map, which this machine does not have (the name is the file's)
        {
            ScreenState state(kIslands);
            MapSelectScreen::RoomView room = guest_view();
            room.map_file = long_stem + ".LVL";
            state.screen.set_room(room);
            Spy spy(arc);
            state.screen.render(spy, arc);
            const SetupLayout& g = SetupLayout::of(SetupVariant::Guest);
            const std::string drawn = name_drawn(spy, long_stem, g.name_text.x, g.name_text.y);
            check(!drawn.empty() && drawn.size() < long_stem.size() && spy.get_text_width(drawn) <= g.name_text.w && spy.get_text_width(drawn) > g.name_text.w - 12,
                  "the room's map name is cut at the box on a guest's screen too: " + std::to_string(drawn.size()) + " of " + std::to_string(long_stem.size()) + " characters, " + std::to_string(spy.get_text_width(drawn)) + " of " + std::to_string(g.name_text.w) + " pixels");
        }
        std::filesystem::remove_all(dir, ignore);
    }
    // a map that this machine does not have, or cannot read, or no map at all: the "No preview" box
    {
        ScreenState state(kIslands);
        MapSelectScreen::RoomView room = guest_view();
        room.map_file = "NOSUCH.LVL";
        state.screen.set_room(room);
        state.screen.set_chat_panel(sample_chat(false));
        Spy spy(arc);
        state.screen.render(spy, arc);
        const SetupLayout& l = SetupLayout::of(SetupVariant::Guest);
        const LayoutRect area = l.preview_area();
        const Spy::Ev* none = spy.find_text("No preview");
        const int32_t tw = spy.get_text_width("No preview"), th = spy.get_text_height(FontSize::Px18);
        check(spy.count(Spy::Kind::Image) == 0, "no map file, no picture");
        check(none != nullptr && none->x == area.x + (area.w - tw) / 2 && none->y == area.y + (area.h - th) / 2 && none->size == FontSize::Px18, "\"No preview\" stands in the middle of the black box");
        check(spy.find_text("Map not on this computer") != nullptr && spy.find_text("NOSUCH") != nullptr && spy.find_text("???") != nullptr, "the caption says so, the name is the room's, the description is \"???\"");
    }
    {
        // a file that is not a map: the entry exists (its description is empty), its picture cannot be made
        const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("ants_wide_setup_maps_" + std::to_string(static_cast<unsigned long long>(SDL_GetPerformanceCounter())));
        std::filesystem::create_directories(dir);
        {
            std::ofstream bad(dir / "BAD.LVL", std::ios::binary);
            bad << "this is not a map";
        }
        MapSelectScreen screen;
        screen.init(dir.string());
        screen.set_wide_layout(true);
        screen.update(1.0f);
        Spy spy(arc);
        screen.render(spy, arc);
        check(screen.get_maps().size() == 1 && spy.count(Spy::Kind::Image) == 0 && spy.find_text("No preview") != nullptr && spy.find_text("Map cannot be read") != nullptr, "a file that is not a map: the \"No preview\" box and \"Map cannot be read\"");
        Spy again(arc);
        screen.render(again, arc);
        check(again.find_text("No preview") != nullptr, "... and it is not tried again to no avail (the same box the next frame)");
        std::error_code ignore;
        std::filesystem::remove_all(dir, ignore);
        MapSelectScreen none;
        none.init("no_such_folder_for_the_wide_setup_test");
        none.set_wide_layout(true);
        none.update(1.0f);
        Spy empty(arc);
        none.render(empty, arc);
        check(none.get_maps().empty() && empty.find_text("No preview") != nullptr && empty.count(Spy::Kind::Image) == 0, "no maps at all: the \"No preview\" box, no crash");
    }
    // the picture is made once per map and kept
    {
        ScreenState state(kTiny);
        Spy a(arc), b(arc);
        state.screen.render(a, arc);
        state.screen.render(b, arc);
        const uint8_t* pa = nullptr;
        const uint8_t* pb = nullptr;
        for (const Spy::Ev& e : a.events) pa = e.kind == Spy::Kind::Image ? e.image : pa;
        for (const Spy::Ev& e : b.events) pb = e.kind == Spy::Kind::Image ? e.image : pb;
        check(pa != nullptr && pa == pb, "the preview is made once for a map: the next frame draws the same bitmap");
        state.screen.set_selected_index(kGauntlet);
        Spy c(arc);
        state.screen.render(c, arc);
        const uint8_t* pc = nullptr;
        uint64_t hc = 0, ha = 0;
        for (const Spy::Ev& e : c.events) {
            if (e.kind == Spy::Kind::Image) {
                pc = e.image;
                hc = e.image_hash;
            }
        }
        for (const Spy::Ev& e : a.events) ha = e.kind == Spy::Kind::Image ? e.image_hash : ha;
        check(pc != nullptr && pc != pa && hc != ha, "another map, another picture");
        state.screen.set_selected_index(kTiny);
        Spy d(arc);
        state.screen.render(d, arc);
        const uint8_t* pd = nullptr;
        for (const Spy::Ev& e : d.events) pd = e.kind == Spy::Kind::Image ? e.image : pd;
        check(pd == pa, "and back: the first picture is still kept");
    }
    // the picture is made for the box that the variant has: the same screen first as a local game (a box of 300), then as a room (248)
    {
        ScreenState state(kIslands);
        Spy single(arc);
        state.screen.render(single, arc);
        state.screen.set_room(leader_view());
        Spy room(arc);
        state.screen.render(room, arc);
        int32_t w_single = 0, w_room = 0;
        for (const Spy::Ev& e : single.events) w_single = e.kind == Spy::Kind::Image ? e.w : w_single;
        for (const Spy::Ev& e : room.events) w_room = e.kind == Spy::Kind::Image ? e.w : w_room;
        check(w_single == 300 && w_room == 240, "the same map is 300 pixels wide in the single player's box and 240 in the room's (the minimap-colour picture of this test renderer: a whole scale of 4 cells in 248) (" + std::to_string(w_single) + ", " + std::to_string(w_room) + ")");
    }
    // a chat panel on a game of one machine has no place: the Single variant has no chat column, whatever was set
    {
        ScreenState state(kTiny);
        state.screen.set_chat_panel(sample_chat(true));
        state.screen.set_fill_footer("Empty seats at START:", "Medium bots");
        Spy spy(arc);
        state.screen.render(spy, arc);
        check(spy.find_text("Chat") == nullptr && spy.find_text("ready in a minute") == nullptr && spy.find_text("Empty seats at START:") == nullptr, "a game of one machine draws no chat column and no footer, whatever was set");
    }
    // the classic page: not touched by any of this
    {
        ScreenState state(kTiny, false);
        Spy spy(arc);
        state.screen.render(spy, arc);
        check(!state.screen.wide_layout() && spy.count(Spy::Kind::Image) == 0 && spy.count(Spy::Kind::Region) == 0, "the classic page draws no preview and no part of a piece");
        const Spy::Ev* name = spy.find_text("TINY");
        check(name != nullptr && name->x == 36 && name->y == 312, "its labels are where the original puts them: the name at (36, 312)");
        state.screen.set_room(leader_view());
        state.screen.set_chat_panel(sample_chat(true));
        state.screen.set_fill_footer("Empty seats at START:", "Medium bots");
        Spy room(arc);
        state.screen.render(room, arc);
        check(room.find_text("Chat") == nullptr && room.find_text("Empty seats at START:") == nullptr, "the chat column and the footer are not on the classic page, whatever was set");
    }
}

// =====================================================================================================================================================
// 6. The pointer
// =====================================================================================================================================================

enum Control { kNone = 0, kLeave, kUp, kDown, kFogOn, kFogOff, kStart };

/// Which button a press at this pixel captures (the hit test is the rectangle of the picture that shows: a press must hit the resting picture and survive the move against the pressed one)
Control control_at(MapSelectScreen& s, int32_t x, int32_t y) {
    s.handle_mouse_motion(x, y);
    s.handle_mouse_down(x, y, SDL_BUTTON_LEFT);
    Control c = kNone;
    if (s.quit_button().pressed()) c = kLeave;
    else if (s.up_button().pressed()) c = kUp;
    else if (s.down_button().pressed()) c = kDown;
    else if (s.fog_on_button().pressed()) c = kFogOn;
    else if (s.fog_off_button().pressed()) c = kFogOff;
    else if (s.start_button().pressed()) c = kStart;
    s.handle_mouse_up(-100, -100, SDL_BUTTON_LEFT);                           // let go away from everything: nothing fires, every button is up again
    s.handle_mouse_motion(-100, -100);
    return c;
}

struct Zone {
    Control control;
    LayoutRect up;          // the resting picture
    LayoutRect pressed;     // the pressed picture
};

/// The zones of the wide screen written out by hand: the classic rectangles of the original (docs 5.50 / 5.52) moved by what moves their pictures
std::vector<Zone> wide_zones(bool host) {
    std::vector<Zone> z = {{kLeave, {845, 12, 99, 22}, {844, 14, 98, 20}}};
    if (host) {
        z.push_back({kUp, {296, 359, 46, 22}, {294, 361, 47, 21}});
        z.push_back({kDown, {296, 383, 47, 20}, {295, 384, 47, 21}});
        z.push_back({kFogOn, {842, 432, 49, 24}, {842, 432, 49, 24}});
        z.push_back({kFogOff, {894, 432, 49, 24}, {894, 432, 49, 24}});
        z.push_back({kStart, {846, 499, 98, 27}, {847, 503, 97, 24}});
    }
    return z;
}

Control expected_at(const std::vector<Zone>& zones, int32_t x, int32_t y) {
    for (const Zone& z : zones) {
        if (z.up.contains(x, y) && z.pressed.contains(x, y)) return z.control;
    }
    return kNone;
}

/// Every pixel of the picture: the control that a press there captures, against the hand-written zones; returns the FNV of the whole map of controls
uint64_t sweep_controls(MapSelectScreen& s, bool host, const std::string& label) {
    const std::vector<Zone> zones = wide_zones(host);
    Fnv f;
    int64_t wrong = 0;
    int64_t counts[7] = {0, 0, 0, 0, 0, 0, 0};
    for (int32_t y = 0; y < 540; ++y) {
        for (int32_t x = 0; x < 960; ++x) {
            const Control got = control_at(s, x, y);
            const Control want = expected_at(zones, x, y);
            if (got != want && wrong++ < 5) std::fprintf(stderr, "  wrong at (%d, %d): control %d, wanted %d\n", x, y, static_cast<int>(got), static_cast<int>(want));
            ++counts[got];
            f.byte(static_cast<uint8_t>(got));
        }
    }
    check(wrong == 0, label + ": a press at every pixel of the 960 x 540 picture captures the control of the layout's zone and nothing else (" + std::to_string(wrong) + " pixels differ)");
    // the zones' sizes: the intersection of the two pictures
    check(counts[kLeave] == 97 * 20, label + ": Leave Game's zone is 97 x 20 pixels (" + std::to_string(counts[kLeave]) + ")");
    if (host) {
        check(counts[kUp] == 45 * 20 && counts[kDown] == 46 * 19 && counts[kFogOn] == 49 * 24 && counts[kFogOff] == 49 * 24 && counts[kStart] == 97 * 23,
              label + ": the zones of Up (45 x 20), Down (46 x 19), Fog On / Off (49 x 24), START! (97 x 23) (" + std::to_string(counts[kUp]) + ", " + std::to_string(counts[kDown]) + ", " + std::to_string(counts[kFogOn]) + ", " +
                  std::to_string(counts[kFogOff]) + ", " + std::to_string(counts[kStart]) + ")");
    } else {
        check(counts[kUp] + counts[kDown] + counts[kFogOn] + counts[kFogOff] + counts[kStart] == 0, label + ": there is no other button on the guest's screen");
    }
    return f.value();
}

void test_pointer(const assets::AssetArchive& arc) {
    group("pointer", "every button of every variant answers at the layout's rectangle and nowhere else (the pictures that show decide), through the screen and through the application");
    (void)arc;
    {
        ScreenState state(kTiny);
        const uint64_t h = sweep_controls(state.screen, true, "single");
        fingerprint("ptr.wide.setup.single", h, 960u * 540u);
    }
    {
        ScreenState state(kIslands);
        state.online(true);
        const uint64_t h = sweep_controls(state.screen, true, "online host (the leader)");
        fingerprint("ptr.wide.setup.online", h, 960u * 540u);
    }
    {
        ScreenState state(kIslands);
        state.guest(true);
        const uint64_t h = sweep_controls(state.screen, false, "guest");
        fingerprint("ptr.wide.setup.guest", h, 960u * 540u);
    }
    // the controls do what they are for, at their centres (the screen alone): Up and Down step the list (wrapping), the fog pair latches, START and Leave call back
    {
        ScreenState state(kSmall);
        MapSelectScreen& s = state.screen;
        int starts = 0, quits = 0;
        std::string started;
        s.set_on_start([&](const std::string& path) {
            ++starts;
            started = path;
        });
        s.set_on_quit([&]() { ++quits; });
        auto click = [&](int32_t x, int32_t y) {
            s.handle_mouse_motion(x, y);
            s.handle_mouse_down(x, y, SDL_BUTTON_LEFT);
            s.handle_mouse_up(x, y, SDL_BUTTON_LEFT);
        };
        check(s.get_selected_index() == kSmall, "the list is at SMALL");
        click(318, 371);
        check(s.get_selected_index() == kMedium, "a click on Up (296, 359) steps back: MEDIUM");
        click(318, 393);
        check(s.get_selected_index() == kSmall, "a click on Down (296, 383) steps on: SMALL");
        s.set_selected_index(0);
        click(318, 371);
        check(s.get_selected_index() == 5, "Up wraps from the first map to the last");
        click(318, 393);
        check(s.get_selected_index() == 0, "Down wraps from the last to the first");
        click(867, 444);
        check(s.is_fog_of_war_enabled(), "Fog On (842, 432) latches the fog on");
        click(919, 444);
        check(!s.is_fog_of_war_enabled(), "Fog Off (894, 432) latches it off");
        click(895, 512);
        check(starts == 1 && started.find("GAUNTLET") != std::string::npos, "START! (846, 499) starts the selected map");
        click(894, 22);
        check(quits == 1, "Leave Game (845, 12) leaves");
        click(560, 450);                                                      // where the classic START! was
        click(580, 25);                                                       // where the classic Leave Game was
        check(starts == 1 && quits == 1, "the classic page's places for START! and Leave Game do nothing on the wide screen");
        s.handle_key_down(SDLK_s);
        s.handle_key_down(SDLK_q);
        check(starts == 2 && quits == 2, "the keys are the original's, unchanged (S starts, Q leaves)");
    }
    // the classic page's buttons are where they always were (a screen that is not wide)
    {
        ScreenState state(kSmall, false);
        MapSelectScreen& s = state.screen;
        int starts = 0;
        s.set_on_start([&](const std::string&) { ++starts; });
        s.handle_mouse_motion(560, 450);
        s.handle_mouse_down(560, 450, SDL_BUTTON_LEFT);
        s.handle_mouse_up(560, 450, SDL_BUTTON_LEFT);
        check(starts == 1 && s.start_button().up_rect() == ButtonRect({526, 439, 98, 27}) && s.start_button().pressed_rect() == ButtonRect({527, 443, 97, 24}), "the classic page: START! is at (526, 439), pressed (527, 443)");
        check(s.quit_button().up_rect() == ButtonRect({525, 12, 99, 22}) && s.up_button().up_rect() == ButtonRect({226, 299, 46, 22}) && s.down_button().up_rect() == ButtonRect({226, 323, 47, 20}) &&
                  s.fog_on_button().up_rect() == ButtonRect({522, 372, 49, 24}) && s.fog_off_button().up_rect() == ButtonRect({574, 372, 49, 24}),
              "... and Leave, Up, Down, Fog On and Off");
        s.set_wide_layout(true);
        check(s.start_button().up_rect() == ButtonRect({846, 499, 98, 27}) && s.up_button().up_rect() == ButtonRect({296, 359, 46, 22}), "made wide, the buttons move with the layout");
        s.set_wide_layout(false);
        check(s.start_button().up_rect() == ButtonRect({526, 439, 98, 27}) && s.quit_button().pressed_rect() == ButtonRect({524, 14, 98, 20}), "made classic again, they are the original's again (pressed pictures too)");
    }
}

// =====================================================================================================================================================
// 7. The application: the picture of each screen, the clicks through the event loop
// =====================================================================================================================================================

SDL_Window* find_window() {
    for (uint32_t i = 1; i < 256; ++i) {
        if (SDL_Window* w = SDL_GetWindowFromID(i)) return w;
    }
    return nullptr;
}

std::filesystem::path temp_ini(const char* stem) {
    static int counter = 0;
    return std::filesystem::temp_directory_path() / (std::string(stem) + "_" + std::to_string(static_cast<unsigned long long>(SDL_GetPerformanceCounter())) + "_" + std::to_string(counter++) + ".ini");
}

/// An application on the dummy video driver (a hidden window the size of its canvas, so that window and picture coordinates are the same), with its settings in a file of this run;
/// it is brought to its setup screen (the loading screen and the quick help are closed by the keys that close them)
struct AppFixture {
    explicit AppFixture(Aspect aspect, bool to_setup = true) : settings(temp_ini("ants_wide_setup")) {
        std::error_code ignore;
        std::filesystem::remove(settings, ignore);
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.aspect = aspect;
        cfg.aspect_given = true;
        cfg.start_in_map_select = true;
        cfg.settings_path = settings.string();
        cfg.screenshot_path = settings.string() + ".png";                            // (a headless application with no screenshot to take stops after ten frames: this one is never taken)
        cfg.screenshot_frames = 1000000000;
        cfg.has_window_size = true;
        cfg.window_w = canvas_width_of(aspect);
        cfg.window_h = canvas_height_of(aspect);
        QuietStdout quiet;
        ok = app.init(cfg);
        window = find_window();
        if (ok && to_setup) {
            app.finish_loading();
            app.quick_help_key(SDLK_RETURN);
        }
    }
    ~AppFixture() {
        app.shutdown();
        std::error_code ignore;
        std::filesystem::remove(settings, ignore);
    }
    AppFixture(const AppFixture&) = delete;
    AppFixture& operator=(const AppFixture&) = delete;

    void motion(int32_t x, int32_t y) {
        SDL_Event e{};
        e.type = SDL_MOUSEMOTION;
        e.motion.windowID = SDL_GetWindowID(window);
        e.motion.x = x;
        e.motion.y = y;
        SDL_PushEvent(&e);
    }
    void button(Uint32 type, int32_t x, int32_t y, Uint8 clicks = 1) {
        SDL_Event e{};
        e.type = type;
        e.button.windowID = SDL_GetWindowID(window);
        e.button.button = SDL_BUTTON_LEFT;
        e.button.state = type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
        e.button.clicks = clicks;
        e.button.x = x;
        e.button.y = y;
        SDL_PushEvent(&e);
    }
    void deliver() { app.run_frame_with_delta(0.001f); }
    void click(int32_t x, int32_t y) {
        motion(x, y);
        button(SDL_MOUSEBUTTONDOWN, x, y);
        deliver();
        button(SDL_MOUSEBUTTONUP, x, y);
        deliver();
    }
    std::vector<uint8_t> canvas_pixels() {
        int w = 0, h = 0;
        SDL_GetRendererOutputSize(app.renderer().get_sdl_renderer(), &w, &h);
        std::vector<uint8_t> px(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u, 0);
        SDL_RenderReadPixels(app.renderer().get_sdl_renderer(), nullptr, SDL_PIXELFORMAT_RGBA32, px.data(), w * 4);
        return px;
    }

    std::filesystem::path settings;
    Application app;
    SDL_Window* window{nullptr};
    bool ok{false};
};

void test_application() {
    group("application", "the picture of the setup screen is the whole 960 x 540 canvas, and so is every other screen (test_wide_pages), the classic 4:3 game is the original's; clicks through the event loop");
    const LayoutRect whole{0, 0, 960, 540};
    {
        AppFixture f(Aspect::Wide16x9, false);
        check(f.ok && f.window != nullptr, "the 16:9 application starts");
        if (!f.ok) return;
        check(f.app.state() == AppState::MapSelect && f.app.map_select().wide_layout(), "a headless game starts on its setup screen, which is the wide one");
        check_rect(f.app.picture(), whole, "the setup screen is the whole 960 x 540 canvas");
        f.app.finish_loading();
        f.deliver();
        check(f.app.state() == AppState::QuickHelp, "the quick help (the loading screen ends in it)");
        check_rect(f.app.picture(), whole, "the quick help is the whole canvas too (its wide page: test_wide_pages; it was the original's page, centred)");
        f.app.quick_help_key(SDLK_RETURN);
        check(f.app.state() == AppState::MapSelect, "then the setup screen");
        check_rect(f.app.picture(), whole, "the setup screen is the whole 960 x 540 canvas");
        check_rect(f.app.renderer().picture(), whole, "... in the renderer too");
        check(f.app.map_select().wide_layout(), "the screen is told to be the wide one");
        // the pointer: the picture's own numbers (the canvas's, now)
        f.motion(890, 512);
        f.deliver();
        check(f.app.mouse_screen_x() == 890 && f.app.mouse_screen_y() == 512 && f.app.map_select().start_button().hovered(), "the pointer at (890, 512) hovers START! (the picture is the canvas: no corner)");
        // a match is the whole canvas too; the results are a page again; back on the setup screen the wide version is there again
        check(f.app.start_game("Original-Ants/Maps/SMALL.LVL"), "a match starts");
        check_rect(f.app.picture(), whole, "the match is the whole canvas");
        f.app.hud().update(f.app.sim().get_world_state(), 100);
        sim::MatchResult result;
        result.is_over = true;
        result.ally = {255, 255, 255, 255};
        result.decide_winners();
        f.app.scorecard().show(result, 0);
        f.app.update_results(0.01f);
        check(f.app.scorecard().is_open(), "the results are open");
        check_rect(f.app.picture(), whole, "the results screen is the whole canvas too (its wide page; it was the original's page, centred)");
        f.app.scorecard().hide();
        f.app.return_to_map_select();
        check(f.app.state() == AppState::MapSelect, "back on the setup screen");
        check_rect(f.app.picture(), whole, "the setup screen is the whole canvas again");
        check(f.app.map_select().wide_layout(), "the screen is the wide one again");
    }
    {
        AppFixture f(Aspect::Classic4x3);
        check(f.ok, "the 4:3 application starts");
        if (!f.ok) return;
        check(f.app.state() == AppState::MapSelect, "on its setup screen");
        check_rect(f.app.picture(), LayoutRect{0, 0, 640, 480}, "the classic picture is the original's 640 x 480");
        check(!f.app.map_select().wide_layout(), "the setup screen is the original's page");
        int starts = 0;
        f.app.map_select().set_on_start([&](const std::string&) { ++starts; });
        f.click(580, 450);
        check(starts == 1, "a click at the original's START! (526, 439) starts");
        f.click(890, 512);
        check(starts == 1, "(and the wide screen's place does nothing there)");
    }
    // clicks through the event loop on the wide screen: every control of every variant
    {
        AppFixture f(Aspect::Wide16x9);
        check(f.ok && f.app.state() == AppState::MapSelect, "the setup screen of the 16:9 game");
        if (!f.ok) return;
        MapSelectScreen& ms = f.app.map_select();
        int starts = 0, quits = 0, requests = 0;
        ms.set_on_start([&](const std::string&) { ++starts; });
        ms.set_on_quit([&]() { ++quits; });
        ms.set_on_request_start([&]() { ++requests; });
        f.deliver();
        ms.set_selected_index(3);
        f.click(318, 371);
        check(ms.get_selected_index() == 2, "Up at (318, 371): the list steps back");
        f.click(318, 393);
        check(ms.get_selected_index() == 3, "Down at (318, 393): the list steps on");
        f.click(295, 360);                                                                                      // on Up's resting picture, off its pressed one: nothing
        check(ms.get_selected_index() == 3, "the strip of Up's resting picture that its pressed picture does not cover does nothing");
        f.click(867, 444);
        check(ms.is_fog_of_war_enabled(), "Fog On at (867, 444)");
        f.click(919, 444);
        check(!ms.is_fog_of_war_enabled(), "Fog Off at (919, 444)");
        f.click(895, 512);
        check(starts == 1 && requests == 0, "START! at (895, 512)");
        f.click(894, 22);
        check(quits == 1, "Leave Game at (894, 22)");
        f.click(580, 450);
        f.click(580, 25);
        check(starts == 1 && quits == 1, "the classic places of START! and Leave Game do nothing");
        // the corners of every zone and the pixels just outside them, through the loop
        struct Edge {
            const char* what;
            int32_t x, y;
            bool inside;
            std::function<int()> counter;
        };
        const std::vector<Edge> edges = {
            {"START! first pixel (847, 503)", 847, 503, true, [&] { return starts; }},         {"START! last pixel (943, 525)", 943, 525, true, [&] { return starts; }},
            {"START! left of it (845, 512)", 845, 512, false, [&] { return starts; }},       {"START! above its pressed picture (900, 501)", 900, 501, false, [&] { return starts; }},
            {"START! right of it (944, 512)", 944, 512, false, [&] { return starts; }},      {"START! below it (900, 526)", 900, 526, false, [&] { return starts; }},
            {"Leave first pixel (845, 14)", 845, 14, true, [&] { return quits; }},           {"Leave last pixel (941, 33)", 941, 33, true, [&] { return quits; }},
            {"Leave above (900, 11)", 900, 11, false, [&] { return quits; }},                {"Leave right of its pressed picture (943, 20)", 943, 20, false, [&] { return quits; }},
        };
        for (const Edge& e : edges) {
            const int before = e.counter();
            f.click(e.x, e.y);
            check((e.counter() - before == 1) == e.inside, std::string("a click at ") + e.what + (e.inside ? " acts" : " does nothing"));
        }
        // the leader of a room: the host's screen; START asks the server; the map and the fog are the room's
        ms.set_room(leader_view());
        starts = 0;
        requests = 0;
        const int32_t index = ms.get_selected_index();
        f.click(318, 371);
        f.click(867, 444);
        check(ms.get_selected_index() == index && !ms.is_fog_of_war_enabled(), "the leader's Up and Fog On change nothing (the room's map and fog)");
        f.click(895, 512);
        check(requests == 1 && starts == 0, "the leader's START! at (895, 512) asks the server");
        quits = 0;
        f.click(894, 22);
        check(quits == 1, "the leader can leave");
        // a guest: Leave Game alone
        ms.set_room(guest_view());
        requests = 0;
        quits = 0;
        f.click(895, 512);
        f.click(318, 371);
        f.click(867, 444);
        check(requests == 0 && starts == 0 && ms.get_selected_index() == index, "a guest's clicks on START!, Up and Fog On do nothing");
        f.click(894, 22);
        check(quits == 1, "a guest's Leave Game works");
    }
    // a whole frame of the application is the mock-up (the cursor and the corner's frame-rate plate masked): the real loop draws the same picture as the screen alone
    {
        AppFixture f(Aspect::Wide16x9);
        if (f.ok) {
            f.app.map_select().set_selected_index(kGauntlet);
            f.app.map_select().update(1.66f);
            f.motion(100, 40);
            f.deliver();
            f.deliver();
            const std::vector<uint8_t> px = f.canvas_pixels();
            std::vector<LayoutRect> masks = text_masks(false, false, false, false);
            masks.push_back({95, 35, 40, 45});                    // the pointer
            masks.push_back({640, 524, 320, 16});                 // the frame rate, its sparkline and the version
            masks.push_back({353, 50, 300, 300});                 // the preview (the game's own render of the map: test_map_preview pins it)
            const uint64_t digest = masked_digest(px, 960, 540, masks);
            check(digest == 0xcfcf2beb520c2777ull, "the application's frame of the setup screen (GAUNTLET) is the mock-up's outside the text, the pointer and the corner: " + hex64(digest));
        }
    }
}

// =====================================================================================================================================================
// 8. Fingerprints
// =====================================================================================================================================================

constexpr Golden kGolden[] = {
    {"ptr.wide.setup.single", 0xe6e68464c2b59b55, 518400ull},
    {"ptr.wide.setup.online", 0xe6e68464c2b59b55, 518400ull},
    {"ptr.wide.setup.guest", 0x30781008ed866525, 518400ull},
    {"screen.wide.setup.before_refresh", 0x906df09d35980f45, 177ull},
    {"screen.wide.setup.tiny", 0x295c40852a788141, 187ull},
    {"screen.wide.setup.gauntlet", 0x6aad12f813d8d47e, 187ull},
    {"screen.wide.setup.islands", 0x63fa1457e88ff067, 187ull},
    {"screen.wide.setup.hover_start", 0x79dad53a8e96ce28, 187ull},
    {"screen.wide.setup.pressed_start", 0xdcb5053abee5fe06, 187ull},
    {"screen.wide.setup.pressed_up", 0xb1f45ba5ee02a0d1, 187ull},
    {"screen.wide.setup.fog_on", 0x60d576af3d61f826, 187ull},
    {"screen.wide.setup.locked", 0x60d576af3d61f826, 187ull},
    {"screen.wide.online.plain", 0x6ecf806da2721b62, 199ull},
    {"screen.wide.online.footer", 0x4db6ab7b5504c9e3, 201ull},
    {"screen.wide.online.chat", 0xc4977231da612cda, 252ull},
    {"screen.wide.online.chat_empty", 0x7889c4b74735b3f3, 246ull},
    {"screen.wide.online.no_map", 0xb3f7e3997cbc4162, 246ull},
    {"screen.wide.guest.plain", 0x1d74e66a65e1ef8b, 196ull},
    {"screen.wide.guest.chat", 0xf3413141c8f47af8, 247ull},
    {"screen.wide.guest.fog_yes", 0x3e3c95792dd3e14f, 248ull},
    {"px.wide.setup.single.tiny", 0x28ca52e9932a5b64, 518400ull},
    {"px.wide.setup.single.hover_start", 0xfed41daaf70690da, 518400ull},
    {"px.wide.setup.single.pressed_start", 0xf520b5efe54fdde5, 518400ull},
    {"px.wide.setup.single.fog_on", 0x40de301d4557325c, 518400ull},
    {"px.wide.setup.online.chat_reserved", 0x44211137c7909fda, 518400ull},
    {"px.wide.setup.online.chat_shown", 0xe80f507abc56da2c, 518400ull},
    {"px.wide.setup.guest.fog_yes", 0xf2e71ac507e7b3a1, 518400ull},
};


void test_fingerprints(const assets::AssetArchive& arc) {
    group("fingerprints", "the draw calls and the pixels of the wide screens in their states, golden numbers of the commit that made them");
    auto calls = [&](const std::string& name, ScreenState& state) {
        Spy spy(arc);
        state.screen.render(spy, arc);
        fingerprint("screen.wide." + name, spy.fingerprint(), spy.events.size());
    };
    {
        MapSelectScreen early;
        early.init(kMapsDir);
        early.set_wide_layout(true);
        Spy spy(arc);
        early.render(spy, arc);
        fingerprint("screen.wide.setup.before_refresh", spy.fingerprint(), spy.events.size());
    }
    {
        ScreenState s(kTiny);
        calls("setup.tiny", s);
    }
    {
        ScreenState s(kGauntlet);
        calls("setup.gauntlet", s);
    }
    {
        ScreenState s(kIslands);
        calls("setup.islands", s);
    }
    {
        ScreenState s(kSmall);
        s.screen.handle_mouse_motion(890, 512);
        calls("setup.hover_start", s);
        s.screen.handle_mouse_down(890, 512, SDL_BUTTON_LEFT);
        calls("setup.pressed_start", s);
        s.screen.handle_mouse_up(0, 0, SDL_BUTTON_LEFT);
        s.screen.handle_mouse_motion(318, 371);
        s.screen.handle_mouse_down(318, 371, SDL_BUTTON_LEFT);
        calls("setup.pressed_up", s);
        s.screen.handle_mouse_up(318, 371, SDL_BUTTON_LEFT);
        s.screen.handle_mouse_motion(867, 444);
        s.screen.handle_mouse_down(867, 444, SDL_BUTTON_LEFT);
        s.screen.handle_mouse_up(867, 444, SDL_BUTTON_LEFT);
        calls("setup.fog_on", s);
        s.screen.lock();
        calls("setup.locked", s);
    }
    {
        ScreenState s(kIslands);
        s.online(false);
        calls("online.plain", s);
        s.screen.set_fill_footer("Empty seats at START:", "Medium bots");
        calls("online.footer", s);
        s.screen.set_chat_panel(sample_chat(true));
        calls("online.chat", s);
        MapSelectScreen::ChatPanel empty = sample_chat(true);
        empty.lines.clear();
        empty.typed.clear();
        empty.caret = false;
        s.screen.set_chat_panel(empty);
        calls("online.chat_empty", s);
        MapSelectScreen::RoomView room = leader_view();
        room.map_file = "NOSUCH.LVL";
        s.screen.set_room(room);
        calls("online.no_map", s);
    }
    {
        ScreenState s(kIslands);
        s.guest(false);
        calls("guest.plain", s);
        s.guest(true);
        calls("guest.chat", s);
        s.screen.follow_host_choice("ISLANDS.LVL", true);
        calls("guest.fog_yes", s);
    }
    // the pixels, with the text areas masked: the states that the mock-ups do not show (the mock-ups' own digests are pinned in the "mockups" group)
    {
        RendererRig rig(arc, 960, 540);
        if (rig.ok) {
            auto px = [&](const std::string& name, ScreenState& state, const std::vector<LayoutRect>& masks) {
                rig.renderer.begin_frame();
                state.screen.render(rig.renderer, arc);
                const std::vector<uint8_t> p = rig.read();
                fingerprint("px.wide.setup." + name, masked_digest(p, 960, 540, masks), 960u * 540u);
            };
            std::vector<LayoutRect> single_masks = text_masks(false, false, false, false);
            single_masks.push_back({349, 46, 308, 309});
            {
                ScreenState s(kTiny);
                px("single.tiny", s, text_masks(false, false, false, false));
                s.screen.handle_mouse_motion(890, 512);
                px("single.hover_start", s, single_masks);
                s.screen.handle_mouse_down(890, 512, SDL_BUTTON_LEFT);
                px("single.pressed_start", s, single_masks);
                s.screen.handle_mouse_up(0, 0, SDL_BUTTON_LEFT);
                s.screen.handle_mouse_motion(867, 444);
                s.screen.handle_mouse_down(867, 444, SDL_BUTTON_LEFT);
                s.screen.handle_mouse_up(867, 444, SDL_BUTTON_LEFT);
                px("single.fog_on", s, single_masks);
            }
            {
                ScreenState s(kIslands);
                s.online(false);
                std::vector<LayoutRect> m = text_masks(true, true, false, false);
                m.push_back({379, 83, 248, 248});
                px("online.chat_reserved", s, m);                       // the chat column's frame is NOT there: the clay shows
                s.screen.set_chat_panel(sample_chat(true));
                s.screen.set_fill_footer("Empty seats at START:", "Medium bots");
                std::vector<LayoutRect> m2 = text_masks(true, true, true, true);
                m2.push_back({379, 83, 248, 248});
                px("online.chat_shown", s, m2);
            }
            {
                ScreenState s(kIslands);
                s.guest(true);
                s.screen.follow_host_choice("ISLANDS.LVL", true);
                std::vector<LayoutRect> m = text_masks(true, true, false, true);
                m.push_back({379, 83, 248, 248});
                px("guest.fog_yes", s, m);
            }
        }
    }
    if (g_print) return;
    // the comparison with the golden table
    for (const Measured& m : g_measured) {
        const Golden* found = nullptr;
        for (const Golden& g : kGolden) {
            if (m.name == g.name) found = &g;
        }
        if (found == nullptr) {
            check(false, "fingerprint " + m.name + ": no golden number");
            continue;
        }
        check(found->hash == m.hash && found->count == m.count, "fingerprint " + m.name + ": expected " + hex64(found->hash) + " / " + std::to_string(found->count) + ", actual " + hex64(m.hash) + " / " + std::to_string(m.count));
    }
    check(g_measured.size() == sizeof(kGolden) / sizeof(kGolden[0]), "every golden number was measured: " + std::to_string(g_measured.size()) + " of " + std::to_string(sizeof(kGolden) / sizeof(kGolden[0])));
}

}  // namespace

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--print") == 0) g_print = true;
    }
    ensure_sdl();
    assets::AssetArchive archive;
    if (!archive.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd")) {
        std::fprintf(stderr, "cannot load ants.chd\n");
        return 2;
    }
    test_layout();
    test_strips(archive);
    test_mockups(archive);
    test_preview(archive);
    test_state(archive);
    test_pointer(archive);
    test_application();
    test_fingerprints(archive);
    if (g_print) {
        std::printf("\nconstexpr Golden kGolden[] = {\n");
        for (const Measured& m : g_measured) std::printf("    {\"%s\", %s, %lluull},\n", m.name.c_str(), hex64(m.hash).c_str(), static_cast<unsigned long long>(m.count));
        std::printf("};\n");
    }
    std::printf("\nwide setup: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
