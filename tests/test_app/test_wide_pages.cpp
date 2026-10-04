// The wide pages (the loading screen, the quick help at the start, the results and the desktop start menu of the original recomposed for the 16:9 picture of 960 x 540;
// include/ants_app/wide_page.hpp, page_layout.hpp, results_layout.hpp, start_menu.hpp):
//   1. the layout: every rectangle of every screen (the six panels of the start menu included) is the owner-approved mock-up's (the rectangle files that the mock-up tool wrote beside its
//      pictures: every element's rectangle at 960 x 540), and the classic rectangles are the original's;
//   2. the seams: the two bottom-edge chains of the results boxes are checked against the art of ants.chd (every junction between runs that do not follow each other is between identical
//      columns, every repeated line lies in a run of identical lines, the lengths, where each chain starts and ends), and the pieces of the wide frame do not overlap (so the order in
//      which they are drawn cannot show);
//   3. the boxes at the original's size: the results boxes built with the original's own pieces at the ORIGINAL'S width (the same construction, the chains of the original's width) are
//      the original's own boxes pixel for pixel (the tiles of re_screen), and the wide chain is the same construction 320 columns wider;
//   4. the loading screen's order (fix of the frame pieces' order): the classic picture draws the pieces of `antslogo` last stored first, as the original does (Capstone: 0x102b8d7, 0x1029924,
//      0x1029987, 0x102a977), and the old stored order came out differently in 145 pixels;
//   5. the composed screens against the mock-up PNGs, pixel for pixel (pinned FNV-1a 64 digests of the mock-ups' own pixels, the text areas masked): the real renderer draws each screen;
//   6. the results' numbers (fix of the counters): the four counters of a row are single-line labels, left aligned at the original's x, clipped to their box and drawn in the original's
//      8 px digits (squeezed): the draw calls, the ink of the real picture ("20", "4" and "10" are three numbers), a number wider than its label is clipped;
//   7. the pointer and the clicks: every control of the new screens answers at the rectangle that is drawn (the quick help's START!, the results' Leave Game, every control of the six
//      panels of the start menu: every pixel of the picture, through the models, and clicks through the application's event loop), and the classic places do not answer on the wide screens;
//   8. the application: the picture of every screen is the whole canvas, the classic 4:3 game's pictures are the original's, the quick help's START! lies on the setup screen's START as
//      the original's own two buttons do;
//   9. fingerprints: the draw calls and the masked pixels of the wide screens in their states, and what a click does at every pixel of the picture (golden numbers of this commit).
// Usage: test_wide_pages [--print]   (--print writes the golden table of the fingerprints and the masks and digests that the mock-ups' digests are computed with, for regeneration; never to
// silence a failure). Exit code 0 when every check passes.
#include <SDL.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "ants_app/application.hpp"
#include "ants_app/canvas_layout.hpp"
#include "ants_app/map_select.hpp"
#include "ants_app/page_layout.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/results_layout.hpp"
#include "ants_app/scorecard.hpp"
#include "ants_app/setup_layout.hpp"
#include "ants_app/start_menu.hpp"
#include "ants_app/ui_anim.hpp"
#include "ants_app/wide_page.hpp"
#include "ants_assets/asset_archive.hpp"
#include "ants_sim/match_stats.hpp"
#include "ants_test_paths.hpp"

using namespace ants;
using namespace ants::app;

namespace {

int g_checks = 0;
int g_failures = 0;
const char* g_group = "";
bool g_print = false;
std::string g_save_dir;        // --save DIR writes the pictures of the mock-up comparisons as DIR/<name>.bmp (to look at what a digest is about)

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

LayoutRect rect_of(const ButtonRect& r) { return LayoutRect{r.x, r.y, r.w, r.h}; }

std::string hex64(uint64_t v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%016llx", static_cast<unsigned long long>(v));
    return buf;
}

void ensure_sdl() {
    SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
    SDL_SetHint(SDL_HINT_AUDIODRIVER, "dummy");
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

bool overlaps(const LayoutRect& a, const LayoutRect& b) { return a.w > 0 && a.h > 0 && b.w > 0 && b.h > 0 && a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom(); }

// =====================================================================================================================================================
// The renderer rig, the recording renderer, pixel helpers
// =====================================================================================================================================================

struct RendererRig {
    RendererRig(const assets::AssetArchive& archive, int32_t w, int32_t h) : width(w), height(h) {
        const QuietStdout quiet;
        ensure_sdl();
        win = SDL_CreateWindow("wide-pages", 0, 0, w, h, SDL_WINDOW_HIDDEN);
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

struct Rgb {
    uint8_t r, g, b;
    bool operator==(const Rgb& o) const { return r == o.r && g == o.g && b == o.b; }
    bool operator!=(const Rgb& o) const { return !(*this == o); }
};

Rgb pixel(const std::vector<uint8_t>& px, int32_t width, int32_t x, int32_t y) {
    const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4u;
    return Rgb{px[i], px[i + 1], px[i + 2]};
}

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

/// How many pixels of two pictures differ (a pixel is equal when its three colour bytes are)
int64_t differing_pixels(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, int32_t w, const LayoutRect& region) {
    int64_t n = 0;
    for (int32_t y = region.y; y < region.bottom(); ++y) {
        for (int32_t x = region.x; x < region.right(); ++x) n += pixel(a, w, x, y) != pixel(b, w, x, y) ? 1 : 0;
    }
    return n;
}

/// The first few pixels where two pictures differ, printed (what a failure of a pixel comparison looks like)
void print_differences(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, int32_t w, const LayoutRect& region, int limit = 24) {
    int shown = 0;
    for (int32_t y = region.y; y < region.bottom() && shown < limit; ++y) {
        for (int32_t x = region.x; x < region.right() && shown < limit; ++x) {
            const Rgb pa = pixel(a, w, x, y), pb = pixel(b, w, x, y);
            if (pa == pb) continue;
            std::fprintf(stderr, "    differs at (%d, %d): %d %d %d / %d %d %d\n", x, y, pa.r, pa.g, pa.b, pb.r, pb.g, pb.b);
            ++shown;
        }
    }
}

/// Records the calls in order (the deterministic measures of a text: 6 pixels a character, the cell height of the font)
class Spy : public IRenderer {
public:
    enum class Kind : uint8_t { Sprite, Region, Named, Fill, Rect, Text, Squeezed, Image, Team, Clip, ClearClip, Origin };
    struct Ev {
        Kind kind{Kind::Sprite};
        std::string name;                     // the sprite's name, the text
        int32_t x{0}, y{0}, w{0}, h{0};       // (a squeezed text: w is the width it is squeezed to)
        int32_t sx{0}, sy{0}, sw{0}, sh{0};
        assets::ColorRGBA colour{};
        FontSize size{FontSize::Px12};
    };

    explicit Spy(const assets::AssetArchive& archive) : arc_(archive) {}

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
    void draw_text(const std::string& s, int32_t x, int32_t y, assets::ColorRGBA c) override { text(Kind::Text, s, x, y, c, FontSize::Px12, 0); }
    void draw_text(const std::string& s, int32_t x, int32_t y, assets::ColorRGBA c, FontSize size) override { text(Kind::Text, s, x, y, c, size, 0); }
    void draw_text_squeezed(const std::string& s, int32_t x, int32_t y, assets::ColorRGBA c, FontSize size, int32_t max_width) override { text(Kind::Squeezed, s, x, y, c, size, max_width); }
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

    std::vector<Ev> events;

    size_t count(Kind k) const {
        size_t n = 0;
        for (const Ev& e : events) n += e.kind == k ? 1u : 0u;
        return n;
    }
    const Ev* find_text(const std::string& s) const {
        for (const Ev& e : events) {
            if ((e.kind == Kind::Text || e.kind == Kind::Squeezed) && e.name == s) return &e;
        }
        return nullptr;
    }
    bool has_sprite_at(const std::string& name, int32_t x, int32_t y) const {
        for (const Ev& e : events) {
            if ((e.kind == Kind::Sprite || e.kind == Kind::Named) && e.name == name && e.x == x && e.y == y) return true;
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
    void text(Kind k, const std::string& s, int32_t x, int32_t y, assets::ColorRGBA c, FontSize size, int32_t max_width) {
        Ev e = base(k);
        e.name = s;
        e.x = x; e.y = y; e.w = max_width; e.colour = c; e.size = size;
        events.push_back(e);
    }
    const assets::AssetArchive& arc_;
};

/// The parts of a frame-0 animation of the original's UI (absolute coordinates), moved: the union of the parts' rectangles, i.e. the rectangle of the picture that shows
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

// =====================================================================================================================================================
// The mock-ups' rectangles (their rectangle files, element by element, as the mock-up tool wrote them)
// =====================================================================================================================================================

struct MockRect {
    const char* name;
    int32_t x, y, w, h;
};

constexpr MockRect kMockLoading[] = {
    {"logo \"Microsoft ants!\"", 185, 53, 593, 270},
    {"credits", 192, 329, 573, 172},
    {"strip (masks the subtitle line of the credits)", 200, 345, 478, 31},
    {"progress bar slot", 389, 478, 234, 8},
};
constexpr MockRect kMockQuickHelp[] = {
    {"left column (How to Win / How to Move / Game Play)", 170, 39, 257, 461},
    {"right column (title, Power Ups, Steps)", 427, 40, 362, 463},
    {"START! button", 849, 497, 98, 27},
    {"START! button, pressed picture (qh_start3)", 848, 498, 97, 24},
};
constexpr MockRect kMockResults[] = {
    {"banner \"Game Results\"", 300, 0, 340, 34},
    {"Leave Game button", 845, 12, 99, 22},
    {"Leave Game button, pressed picture (leave3)", 844, 14, 98, 20},
    {"\"YOUR SCORE\" art", 41, 85, 302, 127},
    {"winner box", 35, 248, 889, 59},
    {"column headers", 662, 114, 259, 133},
    {"\"Winner!\" label", 40, 225, 117, 19},
    {"\"other players...\" label", 40, 310, 203, 25},
    {"others box", 34, 336, 892, 159},
    {"row 1 name", 100, 265, 385, 18},
    {"row 1 score", 805, 265, 49, 18},
    {"row 1 friendly lost", 854, 265, 19, 18},
    {"row 1 enemy killed", 875, 265, 21, 18},
    {"row 1 new hatched", 896, 265, 20, 18},
    {"row 1 portraits (alliance)", 36, 271, 48, 40},
    {"row 2 name", 100, 353, 385, 18},
    {"row 2 score", 805, 353, 49, 18},
    {"row 2 friendly lost", 854, 353, 19, 18},
    {"row 2 enemy killed", 875, 353, 21, 18},
    {"row 2 new hatched", 896, 353, 20, 18},
    {"row 2 portrait", 51, 359, 24, 40},
    {"row 3 name", 100, 403, 385, 18},
    {"row 3 score", 805, 403, 49, 18},
    {"row 3 friendly lost", 854, 403, 19, 18},
    {"row 3 enemy killed", 875, 403, 21, 18},
    {"row 3 new hatched", 896, 403, 20, 18},
    {"row 3 portrait", 51, 409, 24, 40},
};

/// The elements of the six panels of the start menu, in drawing order (the model's elements(), left to right of the mock-up tool's file)
constexpr MockRect kMockMenuMain[] = {
    {"title plate", 280, 28, 400, 44},        {"button \"Single player\"", 320, 148, 320, 50}, {"button \"Join with a code\"", 320, 214, 320, 50},
    {"button \"Host an online match\"", 320, 280, 320, 50}, {"button \"Quit\"", 320, 346, 320, 50}, {"hint line", 200, 506, 560, 14},
};
constexpr MockRect kMockMenuSingle[] = {
    {"title plate", 280, 28, 400, 44},        {"intro line", 220, 120, 520, 22},        {"portrait of the Red seat", 244, 154, 40, 46},  {"Red seat name", 296, 165, 130, 24},
    {"Red seat chooser", 440, 154, 280, 46},  {"portrait of the Blue seat", 244, 210, 40, 46}, {"Blue seat name", 296, 221, 130, 24},   {"Blue seat chooser", 440, 210, 280, 46},
    {"portrait of the Black seat", 244, 266, 40, 46}, {"Black seat name", 296, 277, 130, 24}, {"Black seat chooser", 440, 266, 280, 46}, {"rule text", 220, 320, 520, 42},
    {"button \"Continue\"", 320, 364, 320, 46}, {"button \"Back\"", 320, 418, 320, 40},   {"hint line", 200, 506, 560, 14},
};
constexpr MockRect kMockMenuJoin[] = {
    {"title plate", 280, 28, 400, 44},        {"label \"Your name\"", 270, 120, 420, 22}, {"name field", 270, 144, 420, 34},   {"label \"Room code\"", 270, 190, 420, 22},
    {"room code field", 270, 214, 420, 34},   {"button \"Join\"", 320, 266, 320, 46},    {"button \"Back\"", 320, 320, 320, 40}, {"server line", 220, 484, 520, 14},
    {"hint line", 200, 506, 560, 14},
};
constexpr MockRect kMockMenuHost[] = {
    {"title plate", 280, 28, 400, 44},        {"label \"Map\"", 220, 115, 220, 22},      {"map chooser", 450, 108, 250, 36},     {"label \"Players\"", 220, 155, 220, 22},
    {"players chooser", 450, 148, 250, 36},   {"label \"Empty seats at START\"", 220, 195, 220, 22}, {"fill chooser", 450, 188, 250, 36}, {"fill caption", 400, 226, 350, 14},
    {"label \"Your name\"", 220, 250, 220, 22}, {"name field", 450, 244, 250, 34},       {"button \"Host\"", 320, 296, 320, 44}, {"button \"Back\"", 320, 346, 320, 38},
    {"info text", 220, 396, 520, 40},         {"server line", 220, 484, 520, 14},       {"hint line", 200, 506, 560, 14},
};
constexpr MockRect kMockMenuConnecting[] = {
    {"title plate", 280, 28, 400, 44}, {"connecting text", 220, 180, 520, 100}, {"button \"Cancel\"", 360, 320, 240, 46}, {"hint line", 200, 506, 560, 14},
};
constexpr MockRect kMockMenuRoom[] = {
    {"title plate", 280, 28, 400, 44},        {"caption \"Your room:\"", 220, 112, 520, 28}, {"room code box", 240, 142, 480, 70},  {"info text", 220, 220, 520, 38},
    {"fill sentence", 220, 258, 520, 22},     {"players count", 220, 280, 520, 22},        {"button \"Copy\"", 320, 330, 320, 44}, {"button \"Continue to the room\"", 320, 380, 320, 44},
    {"button \"Back\"", 320, 430, 320, 38},   {"hint line", 200, 506, 560, 14},
};

template <size_t N>
void check_mock_table(const MockRect (&table)[N], const std::vector<LayoutRect>& got, const char* label) {
    check(got.size() == N, std::string(label) + ": the layout has " + std::to_string(N) + " elements (" + std::to_string(got.size()) + ")");
    for (size_t i = 0; i < N && i < got.size(); ++i) check_rect(got[i], LayoutRect{table[i].x, table[i].y, table[i].w, table[i].h}, std::string(label) + ": " + table[i].name);
}

// =====================================================================================================================================================
// 1. The layout
// =====================================================================================================================================================

/// The start menu in a panel, as the mock-ups show it (the settings that the mock-up tool assumed: the name "Player", Treasure, four players, the empty seats stay empty, the default server)
StartMenu make_menu(MenuPanel panel, bool wide) {
    StartMenu menu;
    menu.set_wide_layout(wide);
    MenuSettings settings;
    settings.name = "Player";
    menu.set_settings(settings);
    menu.set_server(ServerAddress{});
    menu.set_own_seat(0);
    menu.show_main();
    const auto click = [&](MenuId id) {
        MenuElement e;
        if (!menu.find_element(id, e)) return;
        const int32_t x = e.rect.x + e.rect.w / 2;
        const int32_t y = e.rect.y + e.rect.h / 2;
        menu.on_mouse_move(x, y);
        menu.on_mouse_down(x, y, SDL_BUTTON_LEFT);
        menu.on_mouse_up(x, y, SDL_BUTTON_LEFT);
    };
    switch (panel) {
        case MenuPanel::Main: break;
        case MenuPanel::Single: click(MenuId::Single); break;
        case MenuPanel::Join: click(MenuId::JoinWithCode); break;
        case MenuPanel::Host: click(MenuId::HostOnline); break;
        case MenuPanel::Connecting: {
            click(MenuId::JoinWithCode);
            click(MenuId::Code);
            menu.on_text("demo-treasure-4p-x7k2m9");
            click(MenuId::Join);
            break;
        }
        case MenuPanel::Room: menu.show_room("demo-treasure-4p-x7k2m9", 1, 4); break;
    }
    // the control that the mock-ups show lit: the first panel's Single player, the first seat, the map, Cancel and Copy under the pointer, the name field clicked on (a field takes the focus by a
    // click or a key, not by the pointer: the Join panel itself selects the room code when the name is there)
    const MenuId first[6] = {MenuId::Single, MenuId::Seat1, MenuId::Name, MenuId::HostMap, MenuId::Cancel, MenuId::Copy};
    MenuElement lit;
    if (menu.find_element(first[static_cast<size_t>(panel)], lit)) {
        const int32_t x = lit.rect.x + lit.rect.w / 2;
        const int32_t y = lit.rect.y + lit.rect.h / 2;
        menu.on_mouse_move(x, y);
        if (lit.kind == MenuKind::Field) {
            menu.on_mouse_down(x, y, SDL_BUTTON_LEFT);
            menu.on_mouse_up(x, y, SDL_BUTTON_LEFT);
        }
    }
    return menu;
}

std::vector<LayoutRect> element_rects(const StartMenu& menu) {
    std::vector<LayoutRect> out;
    for (const MenuElement& e : menu.elements()) out.push_back(rect_of(e.rect));
    return out;
}

void test_layout(const assets::AssetArchive& arc) {
    group("layout", "every rectangle of every screen is the mock-up's (the six panels of the start menu too), the classic ones are the original's, the right items move by 320 and the middle by 30");
    // the loading screen
    {
        const LoadingLayout& c = LoadingLayout::classic();
        const LoadingLayout& w = LoadingLayout::wide();
        const LayoutRect wide_rects[] = {w.logo, w.credits, w.strip, w.bar};
        for (size_t i = 0; i < 4; ++i) check_rect(wide_rects[i], LayoutRect{kMockLoading[i].x, kMockLoading[i].y, kMockLoading[i].w, kMockLoading[i].h}, std::string("loading, wide: ") + kMockLoading[i].name);
        check_rect(c.logo, LayoutRect{25, 23, 593, 270}, "loading, classic: the logo");
        check_rect(c.credits, LayoutRect{32, 299, 573, 172}, "loading, classic: the credits");
        check_rect(c.strip, LayoutRect{40, 315, 478, 31}, "loading, classic: the strip");
        check_rect(c.bar, LayoutRect{229, 448, 234, 8}, "loading, classic: the progress bar's slot");
        check(w.logo == c.logo.moved(160, 30) && w.credits == c.credits.moved(160, 30) && w.strip == c.strip.moved(160, 30) && w.bar == c.bar.moved(160, 30), "loading: the wide page's composition is the original's moved as ONE group by (160, 30)");
        // the art's own sizes are the layout's
        const char* names[3] = {"logo.bmp", "credits.bmp", "strip.bmp"};
        const LayoutRect* rects[3] = {&c.logo, &c.credits, &c.strip};
        for (size_t i = 0; i < 3; ++i) {
            const assets::Sprite* s = arc.find_sprite(names[i]);
            check(s != nullptr && static_cast<int32_t>(s->width) == rects[i]->w && static_cast<int32_t>(s->height) == rects[i]->h, std::string("loading: ") + names[i] + " is as big as its rectangle");
        }
        check(c.bar_fill(0) == 0 && c.bar_fill(14) == 131 && c.bar_fill(25) == 234 && c.bar_fill(30) == 234 && c.bar_fill(-3) == 0 && w.bar_fill(14) == 131, "loading: the bar's fill is ticks * 234 / 25, from 0 to 234 (131 after 14 ticks: the mock-up's)");
        check(LoadingLayout::of(true).logo == w.logo && LoadingLayout::of(false).logo == c.logo, "loading: of(wide) picks the wide layout");
    }
    // the quick help
    {
        const QuickHelpLayout& c = QuickHelpLayout::classic();
        const QuickHelpLayout& w = QuickHelpLayout::wide();
        const LayoutRect wide_rects[] = {w.left, w.right, w.start, w.start_pressed};
        for (size_t i = 0; i < 4; ++i) check_rect(wide_rects[i], LayoutRect{kMockQuickHelp[i].x, kMockQuickHelp[i].y, kMockQuickHelp[i].w, kMockQuickHelp[i].h}, std::string("quick help, wide: ") + kMockQuickHelp[i].name);
        check_rect(c.left, LayoutRect{10, 9, 257, 461}, "quick help, classic: the left column");
        check_rect(c.right, LayoutRect{267, 10, 362, 463}, "quick help, classic: the right column");
        check_rect(c.start, LayoutRect{529, 437, 98, 27}, "quick help, classic: START!");
        check_rect(c.start_pressed, LayoutRect{528, 438, 97, 24}, "quick help, classic: START! pressed");
        check(w.left == c.left.moved(160, 30) && w.right == c.right.moved(160, 30), "quick help: the two columns are centred (moved by (160, 30))");
        check(w.start == c.start.moved(320, 60) && w.start_pressed == c.start_pressed.moved(320, 60), "quick help: START! is anchored to the bottom right corner (moved by (320, 60))");
        check(w.left.right() == w.right.x && c.left.right() == c.right.x, "quick help: the two bitmaps still abut (the title and the subtitle line run across the join)");
        const assets::Sprite* qh1 = arc.find_sprite("qh1.bmp");
        const assets::Sprite* qh2 = arc.find_sprite("qh2.bmp");
        check(qh1 != nullptr && qh2 != nullptr && static_cast<int32_t>(qh1->width) == c.left.w && static_cast<int32_t>(qh1->height) == c.left.h && static_cast<int32_t>(qh2->width) == c.right.w && static_cast<int32_t>(qh2->height) == c.right.h,
              "quick help: qh1.bmp and qh2.bmp are as big as their rectangles");
        // the button's pictures are the layout's rectangles (the union of the parts of the animations, moved)
        check_rect(anim_rect(arc, "qh_start1", 320, 60), w.start, "quick help: qh_start1 moved by (320, 60) is START!'s rectangle");
        check_rect(anim_rect(arc, "qh_start2", 320, 60), w.start, "quick help: so is qh_start2");
        check_rect(anim_rect(arc, "qh_start3", 320, 60), w.start_pressed, "quick help: qh_start3 is the pressed rectangle");
        check_rect(anim_rect(arc, "qh_start1", 0, 0), c.start, "quick help, classic: qh_start1 is START!'s rectangle");
        // START! keeps the original's relation to the setup screen's START: 3 px right and 2 px up, in the original's pictures and in the wide ones
        check(c.start.x - MapSelectScreen::BTN_START_X == 3 && c.start.y - MapSelectScreen::BTN_START_Y == -2, "quick help, classic: START! is 3 px right of and 2 px above the setup screen's START (529, 437 / 526, 439)");
        const SetupLayout& setup = SetupLayout::of(SetupVariant::Single);
        check(w.start.x - setup.start.x == 3 && w.start.y - setup.start.y == -2, "quick help, wide: and so it is in the wide pictures (849, 497 / 846, 499): a click on START! lands on the next screen's START");
    }
    // the results
    {
        const ResultsLayout& c = ResultsLayout::classic();
        const ResultsLayout& w = ResultsLayout::wide();
        check(!c.is_wide && w.is_wide && c.dx == 0 && c.dy == 0 && w.dx == 320 && w.dy == 30, "results: the classic layout moves nothing, the wide one moves the right by 320 and the middle down by 30");
        const LayoutRect art[] = {w.banner, w.leave, w.leave_pressed, w.your_score, w.winner_box, w.headers, w.winner_label, w.others_label, w.others_box};
        for (size_t i = 0; i < 9; ++i) check_rect(art[i], LayoutRect{kMockResults[i].x, kMockResults[i].y, kMockResults[i].w, kMockResults[i].h}, std::string("results, wide: ") + kMockResults[i].name);
        // the rows: the left items stay, the right items move by 320, the rows by 30
        const int32_t row_y[3] = {265, 353, 403};
        for (size_t r = 0; r < 3; ++r) {
            const MockRect* m = &kMockResults[9 + (r == 0 ? 0 : 6 + (r - 1) * 6)];
            const std::string at = "results, wide: row " + std::to_string(r + 1) + " ";
            check(w.row_y(r) == row_y[r] && m[0].y == row_y[r], at + "stands at y = " + std::to_string(row_y[r]));
            check(w.name_x == m[0].x && w.name_w == m[0].w, at + "the name's label (100, 385 wide)");
            for (size_t k = 0; k < 4; ++k) check(w.column_x[k] == m[1 + k].x && w.column_w[k] == m[1 + k].w && m[1 + k].y == row_y[r], at + "number " + std::to_string(k + 1) + ": x " + std::to_string(w.column_x[k]) + ", " + std::to_string(w.column_w[k]) + " wide");
        }
        check(w.name_x == 100 && w.name_w == 385 && w.column_x == std::array<int32_t, 4>{805, 854, 875, 896} && w.column_w == std::array<int32_t, 4>{49, 19, 21, 20}, "results, wide: the label boxes are the original's, the numbers 320 further right");
        // the ants: the first of an alliance hangs over the box's edge as in the original (x 45 stays: the left items do not move), a single team's ant at x 60
        check(w.portrait_pair_x[0] - 9 == 36 && w.portrait_pair_x[0] == 45 && w.portrait_pair_x[1] == 75 && w.portrait_alone_x - 9 == 51 && w.portrait_dy == 20, "results, wide: the ants are at the original's x (45 and 75, or 60) and 20 below the row");
        check(w.row_y(1) + w.portrait_dy - 14 == 359 && w.row_y(0) + w.portrait_dy - 14 == 271, "results, wide: and their sprites' rectangles are the mock-up's (y 271 and 359)");
        // the classic page is the original's numbers
        check_rect(c.banner, LayoutRect{140, 0, 340, 34}, "results, classic: the banner");
        check_rect(c.your_score, LayoutRect{41, 55, 302, 127}, "results, classic: YOUR SCORE");
        check_rect(c.headers, LayoutRect{342, 84, 259, 133}, "results, classic: the column headers");
        check_rect(c.winner_box, LayoutRect{35, 218, 569, 59}, "results, classic: the winner box");
        check_rect(c.others_box, LayoutRect{34, 306, 572, 159}, "results, classic: the others box");
        check_rect(c.winner_label, LayoutRect{40, 195, 117, 19}, "results, classic: Winner!");
        check_rect(c.others_label, LayoutRect{40, 280, 203, 25}, "results, classic: other players...");
        check_rect(c.leave, LayoutRect{525, 12, 99, 22}, "results, classic: Leave Game");
        check_rect(c.leave_pressed, LayoutRect{524, 14, 98, 20}, "results, classic: Leave Game pressed");
        check(c.row_y(0) == 235 && c.row_y(1) == 323 && c.row_y(2) == 373 && c.row_y(3) == 423 && c.name_x == 100 && c.column_x == std::array<int32_t, 4>{485, 534, 555, 576} && c.column_w == std::array<int32_t, 4>{49, 19, 21, 20},
              "results, classic: the rows' places are the original's (Y 235, then 50 i + 273; numbers at 485, 534, 555, 576)");
        // the move: left stays, right + 320, middle + 30, the banner and the button hang from the top
        check(w.your_score == c.your_score.moved(0, 30) && w.winner_label == c.winner_label.moved(0, 30) && w.others_label == c.others_label.moved(0, 30), "results: the left art stays at its x and moves down by 30");
        check(w.headers == c.headers.moved(320, 30), "results: the column headers move right by 320 and down by 30");
        check(w.winner_box == LayoutRect{c.winner_box.x, c.winner_box.y + 30, c.winner_box.w + 320, c.winner_box.h} && w.others_box == LayoutRect{c.others_box.x, c.others_box.y + 30, c.others_box.w + 320, c.others_box.h},
              "results: both boxes keep their left edge and are 320 columns wider");
        check(w.leave == c.leave.moved(320, 0) && w.leave_pressed == c.leave_pressed.moved(320, 0) && w.banner.y == 0 && w.banner.x == c.banner.x + 160, "results: Leave Game is at the top right, the banner hangs from the top edge, centred (x 300)");
        check(w.waiting == c.waiting.moved(0, 30) && c.waiting == LayoutRect{100, 350, 385, 50}, "results: \"Waiting for scores...\" follows the left rule: x stays, y + 30");
        // the art's own sizes
        const struct { const char* name; const LayoutRect* r; } sizes[] = {{"resbanr.bmp", &c.banner}, {"yoscore.bmp", &c.your_score}, {"newstats.bmp", &c.headers}, {"winnr.bmp", &c.winner_label}, {"otherp.bmp", &c.others_label}};
        for (const auto& s : sizes) {
            const assets::Sprite* sp = arc.find_sprite(s.name);
            check(sp != nullptr && static_cast<int32_t>(sp->width) == s.r->w && static_cast<int32_t>(sp->height) == s.r->h, std::string("results: ") + s.name + " is as big as its rectangle");
        }
        check_rect(anim_rect(arc, "leave1", 320, 0), w.leave, "results: leave1 moved by 320 is the Leave Game rectangle");
        check_rect(anim_rect(arc, "leave3", 320, 0), w.leave_pressed, "results: leave3 moved by 320 is the pressed rectangle");
        check_rect(anim_rect(arc, "leave1", 0, 0), c.leave, "results, classic: leave1 is the Leave Game rectangle");
        check(ResultsLayout::of(true).dx == 320 && ResultsLayout::of(false).dx == 0, "results: of(wide) picks the wide layout");
        check(kResultsDigitWidth == 8, "results: the original's digits are 8 px wide (the score label's 49 px hold six of them and a pixel)");
        // no overlap between the new pieces and the boxes
        check(!overlaps(w.your_score, w.winner_label) && !overlaps(w.headers, w.winner_box) && !overlaps(w.winner_box, w.others_label) && !overlaps(w.winner_box, w.others_box) && !overlaps(w.leave, w.banner), "results: the pieces of the wide page do not overlap each other");
        check(w.winner_box.right() <= 960 - 16 && w.others_box.right() <= 960 - 16 && w.others_box.bottom() <= 540 - 16 && w.leave.right() <= 960 - 16 && w.headers.right() <= 960 - 16, "results: everything lies inside the frame (16 px) of the picture");
    }
    // the start menu: every panel
    {
        check_mock_table(kMockMenuMain, element_rects(make_menu(MenuPanel::Main, true)), "menu, first panel");
        check_mock_table(kMockMenuSingle, element_rects(make_menu(MenuPanel::Single, true)), "menu, Single player");
        check_mock_table(kMockMenuJoin, element_rects(make_menu(MenuPanel::Join, true)), "menu, Join with a code");
        check_mock_table(kMockMenuHost, element_rects(make_menu(MenuPanel::Host, true)), "menu, Host an online match");
        check_mock_table(kMockMenuConnecting, element_rects(make_menu(MenuPanel::Connecting, true)), "menu, Connecting");
        check_mock_table(kMockMenuRoom, element_rects(make_menu(MenuPanel::Room, true)), "menu, the room's code");
        // the three groups: the title plate at the top (+160, 0), the hint and the server line at the bottom (+160, +60), everything between centred (+160, +30); the classic menu moves nothing
        for (const MenuPanel panel : {MenuPanel::Main, MenuPanel::Single, MenuPanel::Join, MenuPanel::Host, MenuPanel::Connecting, MenuPanel::Room}) {
            const std::vector<MenuElement> classic = make_menu(panel, false).elements();
            const std::vector<MenuElement> wide = make_menu(panel, true).elements();
            bool same_size = classic.size() == wide.size();
            bool groups_ok = same_size;
            bool sizes_ok = same_size;
            for (size_t i = 0; same_size && i < classic.size(); ++i) {
                const int32_t dy = classic[i].group == MenuGroup::Top ? 0 : (classic[i].group == MenuGroup::Bottom ? 60 : 30);
                groups_ok = groups_ok && wide[i].rect.x == classic[i].rect.x + 160 && wide[i].rect.y == classic[i].rect.y + dy && wide[i].group == classic[i].group;
                sizes_ok = sizes_ok && wide[i].rect.w == classic[i].rect.w && wide[i].rect.h == classic[i].rect.h && wide[i].id == classic[i].id && wide[i].kind == classic[i].kind && wide[i].text == classic[i].text;
            }
            check(groups_ok && sizes_ok, "menu: every element of the panel moves by its group's offset and keeps its size, kind and text (panel " + std::to_string(static_cast<int>(panel)) + ")");
            bool title_top = true, hint_bottom = true;
            for (const MenuElement& e : classic) {
                if (e.kind == MenuKind::Title) title_top = title_top && e.group == MenuGroup::Top;
                if (e.kind == MenuKind::Text && e.tone == MenuTone::Dim && e.rect.y >= 420) hint_bottom = hint_bottom && e.group == MenuGroup::Bottom;
            }
            check(title_top && hint_bottom, "menu: the title plate is the top group, the hint and the server line the bottom group");
        }
        const StartMenu classic_menu = make_menu(MenuPanel::Main, false);
        check(!classic_menu.wide_layout() && make_menu(MenuPanel::Main, true).wide_layout() && StartMenu::kWideDx == 160 && StartMenu::kWideTopDy == 0 && StartMenu::kWideMiddleDy == 30 && StartMenu::kWideBottomDy == 60,
              "menu: the offsets are 160 right, and 0 / 30 / 60 down for the top, middle and bottom groups");
    }
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
    int32_t last;        // the line that the last displayed line equals (the column that the original shows before the corner)
    int32_t jumps;       // the junctions between runs that do not follow each other (each of them is checked to be between identical lines)
    int32_t repeats;     // the lines that are repeated
};

constexpr ExpectedStrip kExpectedStrips[] = {
    {"results.winner.bottom", "efram4100.bmp", true, 824, 28, 71, 1, 0},
    {"results.others.bottom", "efram4100.bmp", true, 827, 27, 73, 0, 0},
};

void check_strip(const assets::AssetArchive& arc, const PieceStrip& s, const ExpectedStrip& want) {
    const std::string at = std::string("strip ") + want.id + ": ";
    check(std::strcmp(s.sprite, want.sprite) == 0 && s.columns == want.columns, at + "the piece " + want.sprite + (want.columns ? " by columns" : " by rows"));
    check(s.length == want.length, at + "makes " + std::to_string(want.length) + " lines (" + std::to_string(s.length) + ")");
    const assets::Sprite* sprite = arc.find_sprite(s.sprite);
    check(sprite != nullptr, at + "the piece exists in the archive");
    if (sprite == nullptr || s.span_count == 0) return;
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
    int32_t bad_junctions = 0, bad_repeats = 0, jumps = 0, repeats_seen = 0;
    for (size_t i = 0; i < s.span_count; ++i) {
        const LineSpan& sp = s.spans[i];
        if (sp.dst_count != sp.src_count) {                             // a repeated line: it lies in a run of identical lines (the run only grows)
            ++repeats_seen;
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
    check(jumps == want.jumps && repeats_seen == want.repeats, at + std::to_string(jumps) + " junctions that jump and " + std::to_string(repeats_seen) + " repeated lines, wanted " + std::to_string(want.jumps) + " and " + std::to_string(want.repeats));
    check(bad_junctions == 0, at + "every junction between runs that do not follow each other is between identical lines (" + std::to_string(bad_junctions) + " are not, of " + std::to_string(jumps) + " jumps)");
    check(bad_repeats == 0, at + "every repeated line lies in a run of identical lines (" + std::to_string(bad_repeats) + " do not)");
    const LineSpan& last_span = s.spans[s.span_count - 1];
    const int32_t last_shown = last_span.dst_count != last_span.src_count ? last_span.src : last_span.src + last_span.src_count - 1;
    check(lines_identical(*sprite, s.columns, last_shown, want.last, s.cross_first, s.cross_count), at + "it ends with a line identical to line " + std::to_string(want.last) + " (what the original shows before the corner)");
}

void test_seams(const assets::AssetArchive& arc) {
    group("seams", "the results boxes' bottom-edge chains: lengths, seams between runs (identical columns only), start and end; the wide frame's pieces do not overlap (the order cannot show)");
    size_t count = 0;
    const PieceStrip* strips = results_strips(count);
    check(count == sizeof(kExpectedStrips) / sizeof(kExpectedStrips[0]), "the results page draws " + std::to_string(sizeof(kExpectedStrips) / sizeof(kExpectedStrips[0])) + " strips (" + std::to_string(count) + ")");
    for (const ExpectedStrip& want : kExpectedStrips) {
        const PieceStrip* found = nullptr;
        for (size_t i = 0; i < count; ++i) {
            if (std::strcmp(strips[i].id, want.id) == 0) found = &strips[i];
        }
        check(found != nullptr, std::string("the strip ") + want.id + " exists");
        if (found != nullptr) check_strip(arc, *found, want);
    }
    // the chains span exactly the columns between the corners: the wide boxes' bottom edges are 824 and 827 columns (the original's 504 and 507, 320 more)
    const ResultsBox& winner = results_winner_box();
    const ResultsBox& others = results_others_box();
    check(winner.right_corner_x + 320 - (winner.left_corner_x + 32) == 824 && others.right_corner_x + 320 - (others.left_corner_x + 32) == 827, "the chains fill the columns between the corners (824 and 827 at 320 columns wider)");
    check(winner.right_corner_x - (winner.left_corner_x + 32) == 504 && others.right_corner_x - (others.left_corner_x + 32) == 507, "... and 504 and 507 at the original's width");
    // the wide frame: the pieces are placed whole and none overlaps another, so that the order they are drawn in cannot show (the loading screen's old stored order differed where the original's own
    // pieces overlap, at the original's 640 x 480 frame)
    {
        Spy spy(arc);
        draw_wide_frame(spy, arc);
        std::vector<LayoutRect> rects;
        int64_t area = 0;
        for (const Spy::Ev& e : spy.events) {
            if (e.kind == Spy::Kind::Named) {
                const assets::Sprite* sp = arc.find_sprite(e.name);
                if (sp != nullptr) rects.push_back(LayoutRect{e.x, e.y, static_cast<int32_t>(sp->width), static_cast<int32_t>(sp->height)});
            } else if (e.kind == Spy::Kind::Region) {
                rects.push_back(LayoutRect{e.x, e.y, e.w, e.h});
            }
        }
        bool disjoint = true;
        for (size_t i = 0; i < rects.size(); ++i) {
            area += static_cast<int64_t>(rects[i].w) * rects[i].h;
            for (size_t j = i + 1; j < rects.size(); ++j) disjoint = disjoint && !overlaps(rects[i], rects[j]);
        }
        check(rects.size() == 44, "the wide frame is 30 whole pieces (4 corners, 13 on the top, 13 on the bottom) and 14 runs of the two side strips (" + std::to_string(rects.size()) + ")");
        check(disjoint, "the pieces of the wide frame do not overlap each other");
        check(area == static_cast<int64_t>(960) * 540 - static_cast<int64_t>(928) * 508, "and they cover exactly the ring of 16 px (" + std::to_string(area) + " pixels)");
    }
}

// =====================================================================================================================================================
// 3. The boxes at the original's size
// =====================================================================================================================================================

const PieceStrip& results_strip(const char* id) {
    size_t count = 0;
    const PieceStrip* table = results_strips(count);
    return find_strip(table, count, id);
}

constexpr uint8_t kMagenta[3] = {255, 0, 255};
constexpr assets::ColorRGBA kMagentaFill{255, 0, 255, 255};

// The original's own arrangement of the bottom edge's tiles (re_screen: efram4100 at x 40, 140, 240, 340, 440 and 500; the last overlaps the one before, so after column 59 of the tile at 440 the
// tile at 500 begins with its column 0: a jump between identical columns, 59 and 99 are the same column, which is why the wide chains may jump the same way); only the columns between the
// corners show (the corners are opaque). The wide chains are other arrangements of the same tiles, 320 columns longer.
constexpr LineSpan kWinnerClassicSpans[] = {{28, 72, 72}, {0, 100, 100}, {0, 100, 100}, {0, 100, 100}, {0, 60, 60}, {0, 72, 72}};       // 504 columns
constexpr LineSpan kOthersClassicSpans[] = {{27, 73, 73}, {0, 100, 100}, {0, 100, 100}, {0, 100, 100}, {0, 60, 60}, {0, 74, 74}};        // 507 columns
const PieceStrip kWinnerClassic{"classic.winner.bottom", "efram4100.bmp", true, 0, 0, 504, kWinnerClassicSpans, sizeof(kWinnerClassicSpans) / sizeof(kWinnerClassicSpans[0])};
const PieceStrip kOthersClassic{"classic.others.bottom", "efram4100.bmp", true, 0, 0, 507, kOthersClassicSpans, sizeof(kOthersClassicSpans) / sizeof(kOthersClassicSpans[0])};

/// The box pieces of the original's re_screen (the corners, the lines, the fill tiles and the side pieces), drawn in the original's order (the last stored first) on whatever is under them
void draw_original_boxes(IRenderer& r, const assets::AssetArchive& arc) {
    const auto* anim = arc.find_animation("re_screen");
    if (anim == nullptr || anim->subitems.empty()) return;
    const auto& frames = anim->subitems[0].frames;
    for (size_t k = frames.size(); k-- > 0;) {
        const std::string& name = arc.get_sprite(frames[k].sprite_index).name;
        const bool box_piece = name.rfind("efram", 0) == 0 || name.rfind("rel50", 0) == 0 || name.rfind("rer50", 0) == 0 || name.rfind("bg50x100", 0) == 0 || name.rfind("efrbg100", 0) == 0;
        if (box_piece) r.draw_sprite(frames[k].sprite_index, frames[k].dx, frames[k].dy);
    }
}

void test_boxes(const assets::AssetArchive& arc) {
    group("boxes", "the results boxes built from the original's own pieces are the original's boxes pixel for pixel at the original's width, and the wide ones are the same construction 320 columns wider");
    RendererRig rig(arc, 640, 480);
    check(rig.ok, "the renderer is up on a 640 x 480 canvas");
    if (!rig.ok) return;
    const LayoutRect canvas{0, 0, 640, 480};
    // the original's boxes: the tiles of re_screen on magenta
    rig.renderer.begin_frame();
    rig.renderer.fill_rect(0, 0, 640, 480, kMagentaFill);
    draw_original_boxes(rig.renderer, arc);
    const std::vector<uint8_t> original = rig.read();
    // the construction at the original's width (dx = dy = 0 and the chains of that width)
    rig.renderer.begin_frame();
    rig.renderer.fill_rect(0, 0, 640, 480, kMagentaFill);
    draw_results_box(rig.renderer, arc, results_winner_box(), 0, 0, kWinnerClassic);
    draw_results_box(rig.renderer, arc, results_others_box(), 0, 0, kOthersClassic);
    const std::vector<uint8_t> built = rig.read();
    const int64_t covered = [&] {
        int64_t n = 0;
        for (int32_t y = 0; y < 480; ++y) {
            for (int32_t x = 0; x < 640; ++x) n += pixel(original, 640, x, y) != Rgb{kMagenta[0], kMagenta[1], kMagenta[2]} ? 1 : 0;
        }
        return n;
    }();
    check(covered > 60000, "the original's boxes are on the picture (" + std::to_string(covered) + " pixels drawn)");
    const int64_t box_diff = differing_pixels(original, built, 640, canvas);
    if (box_diff != 0) print_differences(original, built, 640, canvas);
    check(box_diff == 0, "at the original's width the construction is the original's boxes, pixel for pixel (every pixel of the picture, transparent edges included): " + std::to_string(box_diff) + " pixels differ");
    // a corrupted chain does not pass: the same boxes with the bottom edge's last run shortened by a column show a difference (the comparison sees the bottom edge)
    {
        constexpr LineSpan broken_spans[] = {{28, 72, 72}, {0, 100, 100}, {0, 100, 100}, {0, 100, 100}, {0, 60, 60}, {1, 72, 72}};
        const PieceStrip broken{"broken", "efram4100.bmp", true, 0, 0, 504, broken_spans, sizeof(broken_spans) / sizeof(broken_spans[0])};
        rig.renderer.begin_frame();
        rig.renderer.fill_rect(0, 0, 640, 480, kMagentaFill);
        draw_results_box(rig.renderer, arc, results_winner_box(), 0, 0, broken);
        draw_results_box(rig.renderer, arc, results_others_box(), 0, 0, kOthersClassic);
        check(differing_pixels(original, rig.read(), 640, canvas) > 0, "(the comparison sees a bottom edge whose last run starts one column off)");
    }
    // the wide boxes: the left end and the right end are the original's ends (the right one moved by 320), the rows moved down by 30
    RendererRig wide(arc, 960, 540);
    check(wide.ok, "the renderer is up on a 960 x 540 canvas");
    if (!wide.ok) return;
    wide.renderer.begin_frame();
    wide.renderer.fill_rect(0, 0, 960, 540, kMagentaFill);
    const ResultsLayout& l = ResultsLayout::wide();
    draw_results_box(wide.renderer, arc, results_winner_box(), l.dx, l.dy, results_strip("results.winner.bottom"));
    draw_results_box(wide.renderer, arc, results_others_box(), l.dx, l.dy, results_strip("results.others.bottom"));
    const std::vector<uint8_t> wide_px = wide.read();
    // the ends: the left end is the original's for 60 columns (the corner, the side piece and the start of the lines, the bottom edge starts with the same column); at the right end the last column
    // before the corner (a column of the dither that equals the original's), the corner and the side piece are the original's, moved by 320 and 30 rows. (The dither in between is another
    // arrangement of the same two kinds of column.)
    struct End { const char* what; LayoutRect box; int32_t right_corner_x; };
    const End ends[] = {{"winner", LayoutRect{35, 218, 569, 59}, results_winner_box().right_corner_x}, {"others", LayoutRect{34, 306, 572, 159}, results_others_box().right_corner_x}};
    for (const End& e : ends) {
        int64_t left = 0, right = 0;
        for (int32_t y = 0; y < e.box.h; ++y) {
            for (int32_t x = 0; x < 60; ++x) left += pixel(original, 640, e.box.x + x, e.box.y + y) != pixel(wide_px, 960, e.box.x + x, e.box.y + y + 30) ? 1 : 0;
            for (int32_t x = e.right_corner_x - 1; x < e.box.right(); ++x) right += pixel(original, 640, x, e.box.y + y) != pixel(wide_px, 960, x + 320, e.box.y + y + 30) ? 1 : 0;
        }
        check(left == 0, std::string("the wide ") + e.what + " box's left end (60 columns) is the original's, 30 rows lower (" + std::to_string(left) + " pixels differ)");
        check(right == 0, std::string("the wide ") + e.what + " box's right end (the last column of the edge, the corner, the side piece) is the original's, 320 columns further right and 30 rows lower (" + std::to_string(right) + " pixels differ)");
    }
    // the black inside is one flat colour, the top line is one repeated column: the rows in between are identical all along the box
    {
        bool flat = true;
        for (int32_t x = 100; x < 860; ++x) flat = flat && pixel(wide_px, 960, x, 280) == Rgb{7, 11, 15} && pixel(wide_px, 960, x, 280) == pixel(wide_px, 960, 100, 280);
        check(flat, "the winner box's inside is one flat black (7, 11, 15) along its whole width");
        bool top_repeats = true;
        for (int32_t x = 100; x < 860; ++x) {
            for (int32_t y = 248; y < 252; ++y) top_repeats = top_repeats && pixel(wide_px, 960, x, y) == pixel(wide_px, 960, 100, y);
        }
        check(top_repeats, "the top line is one repeated column (every column of it is the same)");
    }
}

// =====================================================================================================================================================
// 4. The loading screen's order
// =====================================================================================================================================================

void test_loading_order(const assets::AssetArchive& arc) {
    group("loading-order", "the classic loading screen draws the pieces of antslogo last stored first, as the original does; the stored order (what the remake drew before) differs in 145 pixels");
    RendererRig rig(arc, 640, 480);
    check(rig.ok, "the renderer is up on a 640 x 480 canvas");
    if (!rig.ok) return;
    const auto* seq = arc.find_animation("antslogo");
    check(seq != nullptr && !seq->subitems.empty(), "antslogo exists");
    if (seq == nullptr || seq->subitems.empty()) return;
    const auto& frames = seq->subitems[0].frames;
    // what is drawn as a frame piece: everything but the two clay tiles and the three bitmaps (by name, independent of the screen's own list of ids)
    auto frame_piece = [&](size_t k) {
        const std::string& n = arc.get_sprite(frames[k].sprite_index).name;
        return n != "dclay48.bmp" && n != "dclay96.bmp" && n != "strip.bmp" && n != "credits.bmp" && n != "logo.bmp";
    };
    // the antslogo's storage: the strip, the credits and the logo are the first three parts, the frame pieces follow
    check(arc.get_sprite(frames[0].sprite_index).name == "strip.bmp" && arc.get_sprite(frames[1].sprite_index).name == "credits.bmp" && arc.get_sprite(frames[2].sprite_index).name == "logo.bmp",
          "antslogo stores the strip, the credits and the logo first (so the original, which draws the last part first, draws them last: the strip is on top)");
    // the draw calls: the frame pieces come in the order of frame_part_draw_order (the last stored part first), the clay tiles and the bitmaps are not among them
    {
        Spy spy(arc);
        draw_loading_screen(spy, arc, false, 0);
        std::vector<std::pair<std::string, std::pair<int32_t, int32_t>>> drawn;
        for (const Spy::Ev& e : spy.events) {
            if (e.kind == Spy::Kind::Sprite) drawn.push_back({e.name, {e.x, e.y}});
        }
        std::vector<std::pair<std::string, std::pair<int32_t, int32_t>>> want;
        for (const size_t k : frame_part_draw_order(frames.size())) {
            if (frame_piece(k)) want.push_back({arc.get_sprite(frames[k].sprite_index).name, {frames[k].dx, frames[k].dy}});
        }
        check(!want.empty() && drawn == want, "the classic loading screen draws the frame pieces in the order of frame_part_draw_order (" + std::to_string(drawn.size()) + " pieces, the last stored first)");
        check(!spy.events.empty() && spy.events.front().kind == Spy::Kind::Fill && spy.events.front().x == 0 && spy.events.front().y == 0 && spy.events.front().w == 640 && spy.events.front().h == 480 &&
                  spy.events.front().colour.r == 219 && spy.events.front().colour.g == 75 && spy.events.front().colour.b == 19, "... after the page is filled with the original's flat orange (219, 75, 19)");
        const size_t n = spy.events.size();
        check(n >= 4 && spy.events[n - 3].name == "logo.bmp" && spy.events[n - 3].x == 25 && spy.events[n - 3].y == 23 && spy.events[n - 2].name == "credits.bmp" && spy.events[n - 2].x == 32 && spy.events[n - 2].y == 299 &&
                  spy.events[n - 1].name == "strip.bmp" && spy.events[n - 1].x == 40 && spy.events[n - 1].y == 315, "... and before the logo (25, 23), the credits (32, 299) and the strip (40, 315), in that order");
    }
    // the pictures: the screen's own, the order of the original made by the test, and the stored order
    auto compose = [&](bool original_order) {
        rig.renderer.begin_frame();
        rig.renderer.fill_rect(0, 0, 640, 480, assets::ColorRGBA{219, 75, 19, 255});
        if (original_order) {
            for (size_t k = frames.size(); k-- > 0;) {
                if (frame_piece(k)) rig.renderer.draw_sprite(frames[k].sprite_index, frames[k].dx, frames[k].dy);
            }
        } else {
            for (size_t k = 0; k < frames.size(); ++k) {
                if (frame_piece(k)) rig.renderer.draw_sprite(frames[k].sprite_index, frames[k].dx, frames[k].dy);
            }
        }
        rig.renderer.draw_named_sprite("logo.bmp", 25, 23);
        rig.renderer.draw_named_sprite("credits.bmp", 32, 299);
        rig.renderer.draw_named_sprite("strip.bmp", 40, 315);
        return rig.read();
    };
    const std::vector<uint8_t> original = compose(true);
    const std::vector<uint8_t> stored = compose(false);
    rig.renderer.begin_frame();
    draw_loading_screen(rig.renderer, arc, false, 0);
    const std::vector<uint8_t> game = rig.read();
    const LayoutRect canvas{0, 0, 640, 480};
    check(differing_pixels(game, original, 640, canvas) == 0, "the screen's picture is the original's order, pixel for pixel");
    const int64_t differ = differing_pixels(original, stored, 640, canvas);
    check(differ == 145, "the stored order (what the remake drew until now) differs from the original's in 145 pixels, at the overlaps of the left and right strips of the frame (" + std::to_string(differ) + ")");
    check(differing_pixels(game, stored, 640, canvas) == 145, "so the screen's picture is no longer the stored order's");
    // the progress bar: the original's dark purple, growing from the slot's left edge
    rig.renderer.begin_frame();
    draw_loading_screen(rig.renderer, arc, false, 12);
    const std::vector<uint8_t> bar = rig.read();
    check(pixel(bar, 640, 229, 448) == Rgb{31, 23, 51} && pixel(bar, 640, 229 + 111, 455) == Rgb{31, 23, 51} && pixel(bar, 640, 229 + 112, 455) != Rgb{31, 23, 51} && pixel(game, 640, 229, 448) != Rgb{31, 23, 51},
          "the bar after 12 ticks is 112 px of (31, 23, 51) from the slot's left edge (234 * 12 / 25), and empty at the start");
}

// =====================================================================================================================================================
// The screens as the tests draw them
// =====================================================================================================================================================

/// The results of the game's own sample (`--scorecard`: four teams, the first two allied): rows "A & B" 1500 / 17 / 26 / 40, 420 / 20 / 4 / 10, 100 / 25 / 1 / 5, the screen 250 ms old (the rows are built)
sim::MatchResult sample_result() {
    sim::MatchResult mr{};
    mr.is_over = true;
    mr.ally = {1, 0, sim::ALLIANCE_NONE, sim::ALLIANCE_NONE};
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

void open_sample(ScorecardModal& card, bool wide, float seconds = 0.25f) {
    card.set_wide_layout(wide);
    card.set_local_player_name("Ana");
    card.show(sample_result(), 0);
    card.update(seconds);
}

void draw_results_picture(RendererRig& rig, const assets::AssetArchive& arc, ScorecardModal& card) {
    rig.renderer.begin_frame();
    card.render(rig.renderer, arc);
}

/// The text areas of the wide results page (the rows' labels, a little more than the 18 px cells): their pixels depend on the machine's font library
std::vector<LayoutRect> results_masks(const ResultsLayout& l, size_t rows) {
    std::vector<LayoutRect> m;
    for (size_t i = 0; i < rows; ++i) {
        const int32_t y = l.row_y(i);
        m.push_back(LayoutRect{l.name_x - 2, y - 2, l.name_w + 4, 22});
        for (size_t c = 0; c < 4; ++c) m.push_back(LayoutRect{l.column_x[c] - 2, y - 2, l.column_w[c] + 4, 22});
    }
    return m;
}

/// The text areas of a panel of the start menu: the text of the plates, buttons, choosers and fields (their face, edges and arrows stay), the text boxes, the insides of the notice and code boxes
std::vector<LayoutRect> menu_masks(const StartMenu& menu) {
    std::vector<LayoutRect> m;
    for (const MenuElement& e : menu.elements()) {
        const int32_t cell = font_cell_height(e.font);
        const int32_t top = e.rect.y + (e.rect.h - cell) / 2;
        switch (e.kind) {
            case MenuKind::Title:
            case MenuKind::Button: m.push_back(LayoutRect{e.rect.x + 6, top - 2, e.rect.w - 12, cell + 4}); break;
            case MenuKind::Cycler: m.push_back(LayoutRect{e.rect.x + 34, top - 2, e.rect.w - 68, cell + 4}); break;
            case MenuKind::Field: m.push_back(LayoutRect{e.rect.x + 6, top - 2, e.rect.w - 12, cell + 4}); break;
            case MenuKind::Text: m.push_back(LayoutRect{e.rect.x - 2, e.rect.y - 2, e.rect.w + 4, e.rect.h + 4}); break;
            case MenuKind::Notice:
            case MenuKind::Code: m.push_back(LayoutRect{e.rect.x + 4, e.rect.y + 4, e.rect.w - 8, e.rect.h - 8}); break;
            case MenuKind::Portrait: break;
        }
    }
    return m;
}

const MenuPanel kPanels[6] = {MenuPanel::Main, MenuPanel::Single, MenuPanel::Join, MenuPanel::Host, MenuPanel::Connecting, MenuPanel::Room};
const char* const kPanelNames[6] = {"menu_main", "menu_single", "menu_join", "menu_host", "menu_connecting", "menu_room"};

void draw_menu_picture(RendererRig& rig, const assets::AssetArchive& arc, const StartMenu& menu) {
    rig.renderer.begin_frame();
    render_start_menu(rig.renderer, arc, menu);
}

// =====================================================================================================================================================
// 5. The composed screens against the mock-ups, pixel for pixel
// =====================================================================================================================================================

// The digests of the mock-ups' own pixels (FNV-1a 64 over the RGB bytes, the text areas of masked_digest blanked: the masks are the layout's own, printed by --print), made from
// the PNGs that the mock-up tool wrote
constexpr uint64_t kMockLoadingDigest = 0x94eabba28aea947eull;
constexpr uint64_t kMockQuickHelpDigest = 0x94b71b16f7a70dcdull;
constexpr uint64_t kMockResultsDigest = 0xbb947f9aa3aa2a78ull;
constexpr uint64_t kMockMenuDigest[6] = {0x7ba0d0a4ec7fb2ddull, 0x7f200eb25b5cfc11ull, 0x519d85125827ebc9ull, 0x906f3b97b3a5995dull, 0x8980eaac55423631ull, 0x722ef4cf407305e9ull};

/// --save: the picture as a bitmap (RGBA bytes of a canvas)
void save_picture(const std::string& name, const std::vector<uint8_t>& px, int32_t w, int32_t h) {
    if (g_save_dir.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(g_save_dir, ec);
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(const_cast<uint8_t*>(px.data()), w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32);
    if (surface == nullptr) return;
    SDL_SaveBMP(surface, (g_save_dir + "/" + name + ".bmp").c_str());
    SDL_FreeSurface(surface);
}

void print_masks(const char* name, const std::vector<LayoutRect>& masks) {
    std::printf("MASKS %s", name);
    for (const LayoutRect& m : masks) std::printf(" %d,%d,%d,%d", m.x, m.y, m.w, m.h);
    std::printf("\n");
}

void test_mockups(const assets::AssetArchive& arc) {
    group("mockups", "the composed screens are the mock-ups' PNGs, pixel for pixel (text areas masked)");
    RendererRig rig(arc, 960, 540);
    check(rig.ok, "the renderer is up on a 960 x 540 canvas");
    if (!rig.ok) return;
    const LayoutRect canvas{0, 0, 960, 540};
    (void)canvas;
    // the loading screen after 14 ticks (the bar's fill is the mock-up's 131 px): no text on it at all
    {
        rig.renderer.begin_frame();
        draw_loading_screen(rig.renderer, arc, true, 14);
        const std::vector<uint8_t> px = rig.read();
        const uint64_t got = masked_digest(px, 960, 540, {});
        save_picture("loading", px, 960, 540);
        if (g_print) { print_masks("loading", {}); std::printf("GAME loading %s\n", hex64(got).c_str()); }
        check(got == kMockLoadingDigest, "the loading screen is the mock-up's: digest " + hex64(got) + ", the mock-up's is " + hex64(kMockLoadingDigest));
    }
    // the quick help (START! at rest): no text on it either
    {
        rig.renderer.begin_frame();
        draw_quick_help_screen(rig.renderer, arc, true, QuickHelpStart::Up);
        const std::vector<uint8_t> px = rig.read();
        const uint64_t got = masked_digest(px, 960, 540, {});
        save_picture("quickhelp", px, 960, 540);
        if (g_print) { print_masks("quickhelp", {}); std::printf("GAME quickhelp %s\n", hex64(got).c_str()); }
        check(got == kMockQuickHelpDigest, "the quick help is the mock-up's: digest " + hex64(got) + ", the mock-up's is " + hex64(kMockQuickHelpDigest));
    }
    // the results: the game's own sample, 250 ms old (the ants' second frame, the rows built), the rows' text masked
    {
        ScorecardModal card;
        open_sample(card, true);
        draw_results_picture(rig, arc, card);
        const std::vector<uint8_t> px = rig.read();
        const std::vector<LayoutRect> masks = results_masks(ResultsLayout::wide(), 3);
        const uint64_t got = masked_digest(px, 960, 540, masks);
        save_picture("results", px, 960, 540);
        if (g_print) { print_masks("results", masks); std::printf("GAME results %s\n", hex64(got).c_str()); }
        check(card.rows().size() == 3, "the sample has three rows");
        check(got == kMockResultsDigest, "the results screen is the mock-up's outside the rows' text: digest " + hex64(got) + ", the mock-up's is " + hex64(kMockResultsDigest));
    }
    // the six panels of the start menu: the plates, edges, arrows, frames and ants are the mock-ups', the text areas masked
    for (size_t i = 0; i < 6; ++i) {
        const StartMenu menu = make_menu(kPanels[i], true);
        draw_menu_picture(rig, arc, menu);
        const std::vector<uint8_t> px = rig.read();
        const std::vector<LayoutRect> masks = menu_masks(menu);
        const uint64_t got = masked_digest(px, 960, 540, masks);
        save_picture(kPanelNames[i], px, 960, 540);
        if (g_print) { print_masks(kPanelNames[i], masks); std::printf("GAME %s %s\n", kPanelNames[i], hex64(got).c_str()); }
        check(got == kMockMenuDigest[i], std::string(kPanelNames[i]) + " is the mock-up's outside the text: digest " + hex64(got) + ", the mock-up's is " + hex64(kMockMenuDigest[i]));
    }
    // a mask that hides nothing would make every digest above a lie: one changed pixel of the art changes the digest, one in a text area does not
    {
        ScorecardModal card;
        open_sample(card, true);
        draw_results_picture(rig, arc, card);
        std::vector<uint8_t> px = rig.read();
        const std::vector<LayoutRect> masks = results_masks(ResultsLayout::wide(), 3);
        const uint64_t a = masked_digest(px, 960, 540, masks);
        px[(static_cast<size_t>(300) * 960 + 500) * 4u] ^= 0x10;                       // inside the winner box's black
        const uint64_t b = masked_digest(px, 960, 540, masks);
        px[(static_cast<size_t>(300) * 960 + 500) * 4u] ^= 0x10;
        px[(static_cast<size_t>(270) * 960 + 120) * 4u] ^= 0x10;                       // inside the first name's text area
        const uint64_t c = masked_digest(px, 960, 540, masks);
        check(a != b && a == c, "the digest sees a pixel of the art and does not see one in a text area");
    }
}

// =====================================================================================================================================================
// 6. The results' numbers
// =====================================================================================================================================================

/// The columns of a row's band that have ink (any pixel that is not the box's black), left to right: [first, last] of every run of at least one ink column between `from` and `to`
struct Ink {
    int32_t first{-1};
    int32_t last{-1};
};

Ink ink_between(const std::vector<uint8_t>& px, int32_t width, int32_t from, int32_t to, int32_t y0, int32_t y1) {
    Ink r;
    for (int32_t x = from; x < to; ++x) {
        bool any = false;
        for (int32_t y = y0; y < y1; ++y) any = any || pixel(px, width, x, y) != Rgb{7, 11, 15};
        if (any) {
            if (r.first < 0) r.first = x;
            r.last = x;
        }
    }
    return r;
}

void test_counters(const assets::AssetArchive& arc) {
    group("counters", "the four numbers of a row: single-line labels, left aligned at the original's x, clipped to their box, in the original's 8 px digits; \"20\", \"4\" and \"10\" are three numbers");
    for (const bool wide : {false, true}) {
        const ResultsLayout& l = ResultsLayout::of(wide);
        const std::string at = std::string(wide ? "wide" : "classic") + ": ";
        ScorecardModal card;
        open_sample(card, wide);
        Spy spy(arc);
        card.render(spy, arc);
        // every number is Clip, Squeezed text, ClearClip, in the label's box, at the label's x and the row's y
        const struct { const char* text; size_t row; size_t col; } numbers[] = {{"1500", 0, 0}, {"17", 0, 1}, {"26", 0, 2}, {"40", 0, 3}, {"420", 1, 0}, {"20", 1, 1}, {"4", 1, 2}, {"10", 1, 3}, {"100", 2, 0}, {"25", 2, 1}, {"1", 2, 2}, {"5", 2, 3}};
        int found = 0, bracketed = 0;
        for (size_t i = 0; i < spy.events.size(); ++i) {
            const Spy::Ev& e = spy.events[i];
            if (e.kind != Spy::Kind::Squeezed) continue;
            for (const auto& n : numbers) {
                if (e.name != n.text || e.x != l.column_x[n.col] || e.y != l.row_y(n.row)) continue;
                ++found;
                const bool clip_before = i > 0 && spy.events[i - 1].kind == Spy::Kind::Clip && spy.events[i - 1].x == l.column_x[n.col] && spy.events[i - 1].y == l.row_y(n.row) && spy.events[i - 1].w == l.column_w[n.col] && spy.events[i - 1].h == 50;
                const bool clear_after = i + 1 < spy.events.size() && spy.events[i + 1].kind == Spy::Kind::ClearClip;
                if (clip_before && clear_after && e.w == kResultsDigitWidth * static_cast<int32_t>(std::string(n.text).size()) && e.size == FontSize::Px18 && e.colour.r == 239 && e.colour.g == 231 && e.colour.b == 223) ++bracketed;
            }
        }
        check(found == 12 && bracketed == 12, at + "the twelve numbers are drawn clipped to their label's box (x of the column, 50 high) and squeezed to 8 px a digit, 18 px, in the labels' cream (" + std::to_string(found) + ", " + std::to_string(bracketed) + ")");
        const Spy::Ev* name = spy.find_text("Ana & Red");
        check(name != nullptr && name->kind == Spy::Kind::Text && name->x == l.name_x && name->y == l.row_y(0), at + "the name is a label as before (not squeezed): (100, the row's y)");
        check(spy.count(Spy::Kind::Squeezed) == 12 && spy.count(Spy::Kind::Clip) == 12 && spy.count(Spy::Kind::ClearClip) == 12, at + "and nothing else is clipped or squeezed");
    }
    // the real picture: the ink of the sample's rows. Every number stays inside the width of its digits (8 px each, one pixel for the edge's anti-aliasing) and two numbers are never closer than
    // two empty columns, so "20", "4" and "10" are three numbers
    {
        RendererRig rig(arc, 960, 540);
        check(rig.ok, "the renderer is up on a 960 x 540 canvas");
        if (rig.ok) {
            ScorecardModal card;
            open_sample(card, true);
            draw_results_picture(rig, arc, card);
            const std::vector<uint8_t> px = rig.read();
            const ResultsLayout& l = ResultsLayout::wide();
            const char* const texts[3][4] = {{"1500", "17", "26", "40"}, {"420", "20", "4", "10"}, {"100", "25", "1", "5"}};
            for (size_t row = 0; row < 3; ++row) {
                const int32_t y = l.row_y(row);
                Ink ink[4];
                for (size_t c = 0; c < 4; ++c) {
                    const int32_t to = c + 1 < 4 ? l.column_x[c + 1] : l.column_x[3] + l.column_w[3] + 2;      // (the last column's band stops before the box's right edge, the side piece at x 920)
                    ink[c] = ink_between(px, 960, l.column_x[c] - 1, to, y, y + 18);
                }
                bool inside = true, apart = true;
                for (size_t c = 0; c < 4; ++c) {
                    const int32_t width = kResultsDigitWidth * static_cast<int32_t>(std::string(texts[row][c]).size());
                    inside = inside && ink[c].first >= l.column_x[c] - 1 && ink[c].last <= l.column_x[c] + width;
                    if (c + 1 < 4) apart = apart && ink[c + 1].first - ink[c].last >= 3;
                }
                check(ink[0].first >= 0 && ink[1].first >= 0 && ink[2].first >= 0 && ink[3].first >= 0, "the row " + std::to_string(row + 1) + ": every number has ink");
                std::string extents;
                for (size_t c = 0; c < 4; ++c) extents += " [" + std::to_string(ink[c].first) + " .. " + std::to_string(ink[c].last) + " in " + std::to_string(l.column_x[c]) + " .. " + std::to_string(l.column_x[c] + kResultsDigitWidth * static_cast<int32_t>(std::string(texts[row][c]).size())) + "]";
                check(inside, "the row " + std::to_string(row + 1) + ": every number stays inside the width of its digits (8 px each):" + extents);
                check(apart, "the row " + std::to_string(row + 1) + ": two numbers are at least two empty columns apart (" + std::to_string(ink[1].first - ink[0].last) + ", " + std::to_string(ink[2].first - ink[1].last) + ", " + std::to_string(ink[3].first - ink[2].last) + ")");
            }
        }
    }
    // a number wider than its label is cut at the label's box, as the original's label surface cuts it: a counter of four digits in the 19 px label
    {
        RendererRig rig(arc, 960, 540);
        if (rig.ok) {
            sim::MatchResult big = sample_result();
            big.stats[2].friendly_lost = 1234;
            ScorecardModal card;
            card.set_wide_layout(true);
            card.show(big, 2);
            card.update(0.3f);
            draw_results_picture(rig, arc, card);
            const std::vector<uint8_t> px = rig.read();
            const ResultsLayout& l = ResultsLayout::wide();
            const int32_t y = l.row_y(1);
            check(card.rows().size() == 3 && card.rows()[1].numbers[1] == "1234", "the second row (team 2) has 1234 friendly ants lost");
            const Ink cut = ink_between(px, 960, l.column_x[1] - 1, l.column_x[2] - 1, y, y + 18);
            check(cut.first >= 0 && cut.last <= l.column_x[1] + l.column_w[1] - 1, "a counter of four digits is cut at its label's right edge (x " + std::to_string(l.column_x[1] + l.column_w[1]) + "), it does not run into the next column (ink " + std::to_string(cut.first) + " .. " + std::to_string(cut.last) + ")");
        }
    }
}

// =====================================================================================================================================================
// The application as the tests drive it
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

/// What the application starts on: the setup screen (a headless game), the loading screen (a game with a window: SDL's dummy driver), the desktop start menu (headless)
enum class Start { Setup, Loading, Menu };

/// An application on SDL's dummy video driver (a window the size of its canvas, so that window and picture coordinates are the same), its settings in a file of this run
struct AppFixture {
    explicit AppFixture(Aspect aspect, Start start = Start::Setup) : settings(temp_ini("ants_wide_pages")) {
        std::error_code ignore;
        std::filesystem::remove(settings, ignore);
        ApplicationConfig cfg;
        cfg.headless = start != Start::Loading;
        cfg.skip_intro = start != Start::Loading;
        cfg.aspect = aspect;
        cfg.aspect_given = true;
        cfg.start_in_map_select = true;
        cfg.start_menu = start == Start::Menu;
        cfg.lan_port = 0;
        cfg.settings_path = settings.string();
        cfg.screenshot_path = settings.string() + ".png";                            // (a headless application with no screenshot to take stops after ten frames: this one is never taken)
        cfg.screenshot_frames = 1000000000;
        cfg.has_window_size = true;
        cfg.window_w = canvas_width_of(aspect);
        cfg.window_h = canvas_height_of(aspect);
        QuietStdout quiet;
        ok = app.init(cfg);
        window = find_window();
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

    std::filesystem::path settings;
    Application app;
    SDL_Window* window{nullptr};
    bool ok{false};
};

// =====================================================================================================================================================
// 7. The pointer and the clicks
// =====================================================================================================================================================

struct Measured {
    std::string name;
    uint64_t hash;
    uint64_t count;
};
std::vector<Measured> g_measured;
void fingerprint(const std::string& name, uint64_t hash, uint64_t count) { g_measured.push_back({name, hash, count}); }

/// The zone of a ScreenButton over every pixel of the 960 x 540 picture: the pixels where a press captures it (the hit test is the rectangle of the picture that shows: a press must hit the
/// resting picture and survive the move against the pressed one). Returns the hash of the map and the number of pixels of the zone.
uint64_t sweep_button(const ScreenButton& base, int64_t& zone, int32_t w = 960, int32_t h = 540) {
    Fnv f;
    zone = 0;
    for (int32_t y = 0; y < h; ++y) {
        for (int32_t x = 0; x < w; ++x) {
            ScreenButton b = base;
            b.reset();
            const bool hit = b.on_press(x, y) && b.pressed();
            zone += hit ? 1 : 0;
            f.byte(hit ? 1 : 0);
        }
    }
    return f.value();
}

uint64_t sweep_leave(ScorecardModal& card, int64_t& zone) {
    Fnv f;
    zone = 0;
    for (int32_t y = 0; y < 540; ++y) {
        for (int32_t x = 0; x < 960; ++x) {
            card.handle_mouse_motion(x, y);
            card.handle_mouse_down(x, y);
            const bool hit = card.is_quit_pressed();
            card.handle_mouse_up(-100, -100);                                    // let go away from everything: nothing fires, the button is up again
            card.handle_mouse_motion(-100, -100);
            zone += hit ? 1 : 0;
            f.byte(hit ? 1 : 0);
        }
    }
    return f.value();
}

void test_pointer(const assets::AssetArchive& arc) {
    group("pointer", "every control of the new screens answers at the rectangle that is drawn and nowhere else: the quick help's START!, the results' Leave Game, the six panels of the start menu (every pixel), clicks through the loop");
    (void)arc;
    // the quick help's START!: the zone is the intersection of the resting and the pressed picture, 96 x 24 pixels at (849, 498) in the wide page
    {
        AppFixture f(Aspect::Wide16x9);
        check(f.ok && f.window != nullptr, "the 16:9 application starts");
        if (!f.ok) return;
        f.app.finish_loading();
        f.deliver();
        check(f.app.state() == AppState::QuickHelp, "the quick help is up");
        const ScreenButton& b = f.app.quick_help_start_button();
        check(rect_of(b.up_rect()) == QuickHelpLayout::wide().start && rect_of(b.pressed_rect()) == QuickHelpLayout::wide().start_pressed, "the application's button is at the wide page's rectangles");
        int64_t zone = 0;
        const uint64_t h = sweep_button(b, zone);
        check(zone == 96 * 24, "a press at every pixel of the picture captures START! in exactly its 96 x 24 zone (" + std::to_string(zone) + " pixels)");
        fingerprint("ptr.wide.pages.quickhelp", h, 960u * 540u);
        // the corners of the zone and the pixels just outside it, through the event loop: the click ends the quick help (the setup screen follows)
        struct Edge { const char* what; int32_t x, y; bool inside; };
        const Edge edges[] = {{"first pixel (849, 498)", 849, 498, true}, {"last pixel (944, 521)", 944, 521, true}, {"left of it (848, 510)", 848, 510, false}, {"right of it (945, 510)", 945, 510, false},
                              {"above (895, 497)", 895, 497, false}, {"below (895, 522)", 895, 522, false}, {"the classic place (580, 450)", 580, 450, false}, {"the middle of the page (480, 270)", 480, 270, false}};
        for (const Edge& e : edges) {
            AppFixture g(Aspect::Wide16x9);
            g.app.finish_loading();
            g.deliver();
            g.click(e.x, e.y);
            check((g.app.state() == AppState::MapSelect) == e.inside, std::string("a click at ") + e.what + (e.inside ? " ends the quick help" : " does nothing"));
        }
        // the keys are the original's, unchanged
        AppFixture k(Aspect::Wide16x9);
        k.app.finish_loading();
        k.deliver();
        k.app.quick_help_key(SDLK_RETURN);
        check(k.app.state() == AppState::MapSelect, "Enter ends the quick help as before");
    }
    // the classic quick help: START! where it always was
    {
        AppFixture f(Aspect::Classic4x3);
        f.app.finish_loading();
        f.deliver();
        const ScreenButton& b = f.app.quick_help_start_button();
        check(rect_of(b.up_rect()) == LayoutRect{529, 437, 98, 27} && rect_of(b.pressed_rect()) == LayoutRect{528, 438, 97, 24}, "classic: START! is at (529, 437), pressed (528, 438)");
        f.click(580, 450);
        check(f.app.state() == AppState::MapSelect, "classic: a click on START! ends the quick help");
    }
    // the results' Leave Game: 97 x 20 pixels in the wide page (the same zone as the classic one, 320 columns right), only once the rows are there
    {
        ScorecardModal wide;
        open_sample(wide, true);
        int64_t zone = 0;
        const uint64_t h = sweep_leave(wide, zone);
        check(zone == 97 * 20, "the wide results page: a press at every pixel captures Leave Game in exactly its 97 x 20 zone (" + std::to_string(zone) + " pixels)");
        fingerprint("ptr.wide.pages.results", h, 960u * 540u);
        bool inside = true;
        wide.handle_mouse_motion(845, 14);
        wide.handle_mouse_down(845, 14);
        inside = inside && wide.is_quit_pressed();
        wide.handle_mouse_up(-1, -1);
        wide.handle_mouse_motion(941, 33);
        wide.handle_mouse_down(941, 33);
        inside = inside && wide.is_quit_pressed();
        wide.handle_mouse_up(-1, -1);
        check(inside, "the zone's first pixel (845, 14) and last pixel (941, 33) press the button");
        bool left = false;
        wide.set_on_quit([&]() { left = true; });
        wide.handle_mouse_motion(900, 22);
        wide.handle_mouse_down(900, 22);
        wide.handle_mouse_up(900, 22);
        check(left, "a click on Leave Game at (900, 22) leaves");
        left = false;
        wide.handle_mouse_motion(580, 25);                                   // where the classic button is
        wide.handle_mouse_down(580, 25);
        wide.handle_mouse_up(580, 25);
        check(!left, "the classic place of the button (580, 25) does nothing on the wide page");
        ScorecardModal waiting;
        waiting.set_wide_layout(true);
        waiting.show(sample_result(), 0);                                    // (the rows are not built yet: no button)
        waiting.handle_mouse_motion(900, 22);
        waiting.handle_mouse_down(900, 22);
        check(!waiting.is_quit_pressed(), "while the page waits for the scores there is no Leave Game");
        ScorecardModal classic;
        open_sample(classic, false);
        int64_t classic_zone = 0;
        sweep_leave(classic, classic_zone);
        check(classic_zone == 97 * 20, "classic: the zone is the same 97 x 20 pixels");
        classic.handle_mouse_motion(580, 25);
        classic.handle_mouse_down(580, 25);
        check(classic.is_quit_pressed(), "classic: the button is at (525, 12): a press at (580, 25) captures it");
    }
    // the same through the application: the results of a match, clicked in the event loop
    {
        AppFixture f(Aspect::Wide16x9);
        check(f.app.start_game("Original-Ants/Maps/SMALL.LVL"), "a match starts");
        f.app.hud().update(f.app.sim().get_world_state(), 100);
        int quits = 0;
        f.app.scorecard().set_on_quit([&]() { ++quits; });
        f.app.scorecard().show(sample_result(), 0);
        f.app.update_results(0.3f);
        f.deliver();
        check(f.app.scorecard().is_open() && !f.app.scorecard().is_waiting() && f.app.picture() == (LayoutRect{0, 0, 960, 540}), "the results are open (rows built), the whole canvas");
        f.click(580, 25);
        check(quits == 0, "a click at the classic Leave Game place does nothing");
        f.click(900, 22);
        check(quits == 1, "a click on Leave Game at (900, 22) leaves");
        f.click(845, 14);
        f.click(941, 33);
        check(quits == 3, "the first and the last pixel of the zone leave");
        f.click(844, 20);
        f.click(942, 25);
        f.click(900, 34);
        f.click(900, 13);
        check(quits == 3, "the pixels just outside the zone do nothing (left, right, below, above)");
    }
    // the start menu: every control of every panel answers at its rectangle (every pixel of the picture), the rectangles of a control do not overlap another's
    {
        for (size_t i = 0; i < 6; ++i) {
            const StartMenu menu = make_menu(kPanels[i], true);
            const std::vector<MenuElement> all = menu.elements();
            Fnv h;
            std::map<int, int64_t> zone;
            int64_t wrong = 0;
            for (int32_t y = 0; y < 540; ++y) {
                for (int32_t x = 0; x < 960; ++x) {
                    const MenuId got = menu.control_at(x, y);
                    MenuId want = MenuId::None;
                    for (size_t k = all.size(); k-- > 0;) {
                        if (all[k].id != MenuId::None && all[k].rect.contains(x, y)) {
                            want = all[k].id;
                            break;
                        }
                    }
                    wrong += got != want ? 1 : 0;
                    ++zone[static_cast<int>(got)];
                    h.byte(static_cast<uint8_t>(got));
                }
            }
            check(wrong == 0, std::string(kPanelNames[i]) + ": a pointer at every pixel of the picture finds the control whose rectangle is drawn there, and nothing elsewhere (" + std::to_string(wrong) + " pixels differ)");
            bool areas = true;
            for (const MenuElement& e : all) {
                if (e.id != MenuId::None) areas = areas && zone[static_cast<int>(e.id)] == static_cast<int64_t>(e.rect.w) * e.rect.h;
            }
            check(areas, std::string(kPanelNames[i]) + ": every control's zone is exactly its rectangle (no control lies over another)");
            fingerprint(std::string("ptr.wide.pages.") + kPanelNames[i], h.value(), 960u * 540u);
            // the classic page's own places (the original's 640 x 480 page, not moved) are not the wide controls' places: the classic centre of every control is not on that control in the wide menu
            const StartMenu classic = make_menu(kPanels[i], false);
            bool moved_away = true;
            for (const MenuElement& e : classic.elements()) {
                if (e.id == MenuId::None) continue;
                const MenuId at = menu.control_at(e.rect.x + e.rect.w / 2, e.rect.y + e.rect.h / 2);
                moved_away = moved_away && at != e.id;
            }
            check(moved_away, std::string(kPanelNames[i]) + ": the classic place of a control (the middle of its rectangle on the 640 x 480 page) is not that control's place in the wide menu");
        }
    }
    // clicks through the application's loop: the first panel, then a panel's Back, with the pointer's own numbers (the whole canvas)
    {
        AppFixture f(Aspect::Wide16x9, Start::Menu);
        check(f.ok && f.app.state() == AppState::StartMenu, "the 16:9 application starts on its start menu");
        if (!f.ok || f.app.state() != AppState::StartMenu) return;
        check(f.app.picture() == (LayoutRect{0, 0, 960, 540}) && f.app.start_menu().wide_layout(), "the menu is the whole canvas, in its wide layout");
        // (a click that changes the panel makes the rest of its click sequence, within the double-click time, no click of the next panel's: the tests wait that time out)
        const auto click_later = [&](int32_t x, int32_t y) {
            SDL_Delay(600);
            f.click(x, y);
        };
        f.click(480, 173);                                               // Single player (320, 148, 320 x 50)
        check(f.app.start_menu().panel() == MenuPanel::Single, "a click on Single player at (480, 173) opens its panel");
        click_later(480, 438);                                           // Back (320, 418, 320 x 40)
        check(f.app.start_menu().panel() == MenuPanel::Main, "a click on Back at (480, 438) is back on the first panel");
        click_later(320, 143);                                           // where Single player is on the classic page: nothing (y 143 is above the wide button, which starts at 148)
        check(f.app.start_menu().panel() == MenuPanel::Main, "a click at the classic place of Single player does nothing");
        click_later(319, 173);
        click_later(640, 173);
        click_later(480, 147);
        click_later(480, 198);
        check(f.app.start_menu().panel() == MenuPanel::Main, "the pixels just outside Single player (left, right, above, below) do nothing");
        click_later(320, 148);
        check(f.app.start_menu().panel() == MenuPanel::Single, "its first pixel (320, 148) opens the panel");
    }
}

// =====================================================================================================================================================
// 8. The application: the picture of every screen
// =====================================================================================================================================================

void test_application(const assets::AssetArchive& arc) {
    group("application", "every screen is the whole canvas (the wide pages), the classic 4:3 game's pictures are the original's, START! lies over the setup screen's START as the original's buttons do");
    const LayoutRect whole{0, 0, 960, 540};
    const LayoutRect classic{0, 0, 640, 480};
    {
        AppFixture f(Aspect::Wide16x9, Start::Loading);
        check(f.ok && f.window != nullptr, "the 16:9 application with a window starts");
        if (!f.ok) return;
        check(f.app.state() == AppState::Loading, "it starts on the loading screen");
        check_rect(f.app.picture(), whole, "the loading screen is the whole 960 x 540 canvas");
        check_rect(f.app.renderer().picture(), whole, "... in the renderer too");
        f.app.finish_loading();
        f.deliver();
        check(f.app.state() == AppState::QuickHelp, "the quick help follows");
        check_rect(f.app.picture(), whole, "the quick help is the whole canvas");
        check(f.app.quick_help_start_button().up_rect() == ButtonRect({849, 497, 98, 27}), "its START! is the wide page's");
        f.app.quick_help_key(SDLK_RETURN);
        check(f.app.state() == AppState::MapSelect, "then the setup screen");
        check_rect(f.app.picture(), whole, "the setup screen is the whole canvas");
        // START! of the quick help lies over START of the setup screen, 3 px right and 2 px up
        const SetupLayout& setup = SetupLayout::of(SetupVariant::Single);
        check(f.app.map_select().start_button().up_rect().x == 846 && f.app.map_select().start_button().up_rect().y == 499 && setup.start.x == 846 && setup.start.y == 499, "the setup screen's START is at (846, 499)");
        check(QuickHelpLayout::wide().start.x - f.app.map_select().start_button().up_rect().x == 3 && QuickHelpLayout::wide().start.y - f.app.map_select().start_button().up_rect().y == -2, "START! of the quick help is 3 px right and 2 px above it");
        // the results are the whole canvas, with their own wide layout; the screens that follow keep their layouts
        check(f.app.start_game("Original-Ants/Maps/SMALL.LVL"), "a match starts");
        f.app.hud().update(f.app.sim().get_world_state(), 100);
        f.app.scorecard().show(sample_result(), 0);
        f.app.update_results(0.3f);
        check(f.app.scorecard().is_open() && f.app.scorecard().wide_layout(), "the results are open, in their wide layout");
        check_rect(f.app.picture(), whole, "the results screen is the whole canvas");
        f.app.scorecard().hide();
        f.app.return_to_map_select();
        check_rect(f.app.picture(), whole, "back on the setup screen: the whole canvas");
    }
    {
        AppFixture f(Aspect::Wide16x9, Start::Menu);
        check(f.ok && f.app.state() == AppState::StartMenu, "the 16:9 application starts on its start menu");
        if (f.ok) {
            check_rect(f.app.picture(), whole, "the start menu is the whole canvas");
            check(f.app.start_menu().wide_layout(), "and it is told that it is the wide menu");
            // a frame of the menu: the wide frame at the canvas's corners, tile clay inside (the pointer and the corner's plate are the application's)
            f.app.renderer().pin_animation_clock(1500);
            f.app.render_frame();
            std::vector<uint8_t> px(960u * 540u * 4u, 0);
            SDL_RenderReadPixels(f.app.renderer().get_sdl_renderer(), nullptr, SDL_PIXELFORMAT_RGBA32, px.data(), 960 * 4);
            check(pixel(px, 960, 2, 2) != Rgb{219, 75, 19} && pixel(px, 960, 957, 537) != Rgb{219, 75, 19} && pixel(px, 960, 100, 270) != Rgb{0, 0, 0}, "a frame of the menu: the wide frame is at the canvas's corners, the clay is inside it");
        }
    }
    // the classic 4:3 game: the original's picture, the original's places
    {
        AppFixture f(Aspect::Classic4x3, Start::Loading);
        check(f.ok && f.app.state() == AppState::Loading, "the 4:3 application with a window starts on the loading screen");
        if (f.ok) {
            check_rect(f.app.picture(), classic, "classic: the loading screen is the whole 640 x 480 canvas");
            f.app.finish_loading();
            f.deliver();
            check_rect(f.app.picture(), classic, "classic: the quick help is the whole canvas");
            check(f.app.quick_help_start_button().up_rect() == ButtonRect({529, 437, 98, 27}), "classic: START! is the original's (529, 437)");
            f.app.quick_help_key(SDLK_RETURN);
            check(f.app.start_game("Original-Ants/Maps/SMALL.LVL"), "classic: a match starts");
            f.app.hud().update(f.app.sim().get_world_state(), 100);
            f.app.scorecard().show(sample_result(), 0);
            f.app.update_results(0.3f);
            check(f.app.scorecard().is_open() && !f.app.scorecard().wide_layout(), "classic: the results are the original's page");
            check_rect(f.app.picture(), classic, "classic: the results are the whole canvas");
        }
    }
    {
        AppFixture f(Aspect::Classic4x3, Start::Menu);
        check(f.ok && f.app.state() == AppState::StartMenu && !f.app.start_menu().wide_layout(), "classic: the start menu is the original-sized page, not the wide menu");
        if (f.ok) check_rect(f.app.picture(), classic, "classic: the start menu is the whole 640 x 480 canvas");
    }
    // the wide pages are drawn by one function for both pictures: the classic one is the original's page exactly as before (the pieces and their places)
    {
        Spy spy(arc);
        draw_quick_help_screen(spy, arc, false, QuickHelpStart::Up);
        check(spy.has_sprite_at("bstart1.bmp", 529, 437) && spy.count(Spy::Kind::Region) == 0 && spy.count(Spy::Kind::Fill) == 0, "classic: the quick help is the qh_screen composite and the button at the original's place (no part of a piece, no fill)");
        Spy wide(arc);
        draw_quick_help_screen(wide, arc, true, QuickHelpStart::Up);
        check(wide.has_sprite_at("qh1.bmp", 170, 39) && wide.has_sprite_at("qh2.bmp", 427, 40) && wide.has_sprite_at("bstart1.bmp", 849, 497), "wide: the two columns at (170, 39) and (427, 40), START! at (849, 497)");
        Spy hover(arc);
        draw_quick_help_screen(hover, arc, true, QuickHelpStart::Hover);
        Spy pressed(arc);
        draw_quick_help_screen(pressed, arc, true, QuickHelpStart::Pressed);
        check(hover.has_sprite_at("bstart2.bmp", 849, 497) && pressed.has_sprite_at("bstart3.bmp", 848, 498), "wide: the hovered and the pressed button's pictures at their rectangles");
    }
}

// =====================================================================================================================================================
// 9. Fingerprints
// =====================================================================================================================================================

struct Golden {
    const char* name;
    uint64_t hash;
    uint64_t count;
};

constexpr Golden kGolden[] = {
    {"ptr.wide.pages.quickhelp", 0x106da6e002748a25, 518400ull},
    {"ptr.wide.pages.results", 0x30781008ed866525, 518400ull},
    {"ptr.wide.pages.menu_main", 0x1087e844bb59f825, 518400ull},
    {"ptr.wide.pages.menu_single", 0x54df772fbca48b75, 518400ull},
    {"ptr.wide.pages.menu_join", 0x34549dbe8ad8413d, 518400ull},
    {"ptr.wide.pages.menu_host", 0xdb1d7f3d435d96f5, 518400ull},
    {"ptr.wide.pages.menu_connecting", 0x3f6f7ab6ef755ea5, 518400ull},
    {"ptr.wide.pages.menu_room", 0xc91cf83b9fa919a5, 518400ull},
    {"screen.wide.pages.loading.t0", 0x2ede1097bf7bcbeb, 48ull},
    {"screen.wide.pages.loading.t14", 0x7727810729ff18d0, 49ull},
    {"screen.wide.pages.loading.t25", 0x1ee4025a5fbb9d8b, 49ull},
    {"screen.wide.pages.loading.classic_t14", 0x54dba2ad1fb854ee, 35ull},
    {"screen.wide.pages.quickhelp.rest", 0x6daa3f9a1b92e026, 48ull},
    {"screen.wide.pages.quickhelp.hover", 0x10ba0530b8d516e1, 48ull},
    {"screen.wide.pages.quickhelp.pressed", 0x65dab220ed442bee, 48ull},
    {"screen.wide.pages.results.waiting", 0xafd37fce1ee10fa5, 149ull},
    {"screen.wide.pages.results.rows", 0x9a0792d4170f7fb4, 200ull},
    {"screen.wide.pages.results.hover_leave", 0x3477e747fa7d7b2b, 200ull},
    {"screen.wide.pages.results.pressed_leave", 0xda957d5d193423c7, 200ull},
    {"screen.wide.pages.results.big_numbers", 0x5eb6de5aa4403d36, 200ull},
    {"screen.wide.pages.results.classic_rows", 0x9e0737830f145ffa, 201ull},
    {"screen.wide.pages.menu_main", 0x0af18420436d3565, 160ull},
    {"screen.wide.pages.menu_single", 0x035577b7d780f805, 221ull},
    {"screen.wide.pages.menu_join", 0xc2108141358be108, 162ull},
    {"screen.wide.pages.menu_host", 0x68f7eab483e4ee42, 224ull},
    {"screen.wide.pages.menu_connecting", 0x9baf3e53f92d4060, 128ull},
    {"screen.wide.pages.menu_room", 0x1898efe83479baa8, 164ull},
    {"px.wide.pages.loading.t0", 0xc849e041f5dc9132, 518400ull},
    {"px.wide.pages.loading.t14", 0x94eabba28aea947e, 518400ull},
    {"px.wide.pages.loading.t25", 0x059faca4c56039ba, 518400ull},
    {"px.wide.pages.quickhelp.rest", 0x94b71b16f7a70dcd, 518400ull},
    {"px.wide.pages.quickhelp.hover", 0x6e22163363c6455e, 518400ull},
    {"px.wide.pages.quickhelp.pressed", 0xc2f29a11e025273b, 518400ull},
    {"px.wide.pages.results.waiting", 0xdf33f96df0ded173, 518400ull},
    {"px.wide.pages.results.rows", 0xbb947f9aa3aa2a78, 518400ull},
    {"px.wide.pages.results.hover_leave", 0x1f5117ddd7aac5f7, 518400ull},
    {"px.wide.pages.results.pressed_leave", 0x1a9b318e13f9e4c6, 518400ull},
    {"px.wide.pages.results.big_numbers", 0xe44e9c4950a7f6e4, 518400ull},
    {"px.wide.pages.results.four_rows", 0x3968ebeaa37249bc, 518400ull},
    {"px.wide.pages.menu_main", 0x7ba0d0a4ec7fb2dd, 518400ull},
    {"px.wide.pages.menu_single", 0x7f200eb25b5cfc11, 518400ull},
    {"px.wide.pages.menu_join", 0x519d85125827ebc9, 518400ull},
    {"px.wide.pages.menu_host", 0x906f3b97b3a5995d, 518400ull},
    {"px.wide.pages.menu_connecting", 0x8980eaac55423631, 518400ull},
    {"px.wide.pages.menu_room", 0x722ef4cf407305e9, 518400ull},
    {"px.wide.pages.menu_join.notice", 0x86253223cf31f79d, 518400ull},
    {"px.wide.pages.menu_host.notice", 0xb96a17e94f129ec9, 518400ull},
    {"px.wide.pages.menu_main.notice", 0xfb2796a527da148d, 518400ull},
};

void test_fingerprints(const assets::AssetArchive& arc) {
    group("fingerprints", "the draw calls and the pixels of the wide screens in their states, golden numbers of the commit that made them (the pointer's are measured by the pointer group)");
    // ---- the draw calls (a recording renderer: 6 pixels a character)
    auto calls = [&](const std::string& name, const std::function<void(IRenderer&)>& draw) {
        Spy spy(arc);
        draw(spy);
        fingerprint("screen.wide.pages." + name, spy.fingerprint(), spy.events.size());
    };
    calls("loading.t0", [&](IRenderer& r) { draw_loading_screen(r, arc, true, 0); });
    calls("loading.t14", [&](IRenderer& r) { draw_loading_screen(r, arc, true, 14); });
    calls("loading.t25", [&](IRenderer& r) { draw_loading_screen(r, arc, true, 25); });
    calls("loading.classic_t14", [&](IRenderer& r) { draw_loading_screen(r, arc, false, 14); });
    calls("quickhelp.rest", [&](IRenderer& r) { draw_quick_help_screen(r, arc, true, QuickHelpStart::Up); });
    calls("quickhelp.hover", [&](IRenderer& r) { draw_quick_help_screen(r, arc, true, QuickHelpStart::Hover); });
    calls("quickhelp.pressed", [&](IRenderer& r) { draw_quick_help_screen(r, arc, true, QuickHelpStart::Pressed); });
    {
        ScorecardModal waiting;
        waiting.set_wide_layout(true);
        waiting.show(sample_result(), 0);
        calls("results.waiting", [&](IRenderer& r) { waiting.render(r, arc); });
        ScorecardModal rows;
        open_sample(rows, true);
        calls("results.rows", [&](IRenderer& r) { rows.render(r, arc); });
        ScorecardModal hover;
        open_sample(hover, true);
        hover.handle_mouse_motion(900, 22);
        calls("results.hover_leave", [&](IRenderer& r) { hover.render(r, arc); });
        ScorecardModal pressed;
        open_sample(pressed, true);
        pressed.handle_mouse_motion(900, 22);
        pressed.handle_mouse_down(900, 22);
        calls("results.pressed_leave", [&](IRenderer& r) { pressed.render(r, arc); });
        sim::MatchResult big = sample_result();
        big.stats[2].friendly_lost = 1234;
        big.stats[3].new_hatched = 987;
        ScorecardModal wide_big;
        wide_big.set_wide_layout(true);
        wide_big.show(big, 3);
        wide_big.update(0.3f);
        calls("results.big_numbers", [&](IRenderer& r) { wide_big.render(r, arc); });
        ScorecardModal classic;
        open_sample(classic, false);
        calls("results.classic_rows", [&](IRenderer& r) { classic.render(r, arc); });
    }
    for (size_t i = 0; i < 6; ++i) {
        const StartMenu menu = make_menu(kPanels[i], true);
        calls(std::string(kPanelNames[i]), [&](IRenderer& r) { render_start_menu(r, arc, menu); });
    }
    // ---- the pixels, text masked: the states that the mock-ups do not show (the mock-ups' own digests are pinned in the "mockups" group)
    RendererRig rig(arc, 960, 540);
    check(rig.ok, "the renderer is up on a 960 x 540 canvas");
    if (rig.ok) {
        auto px = [&](const std::string& name, const std::vector<LayoutRect>& masks) {
            const std::vector<uint8_t> p = rig.read();
            std::string file = name;
            std::replace(file.begin(), file.end(), '.', '_');
            save_picture("fp_" + file, p, 960, 540);
            fingerprint("px.wide.pages." + name, masked_digest(p, 960, 540, masks), 960u * 540u);
        };
        auto loading = [&](const char* name, int32_t ticks) {
            rig.renderer.begin_frame();
            draw_loading_screen(rig.renderer, arc, true, ticks);
            px(name, {});
        };
        loading("loading.t0", 0);
        loading("loading.t14", 14);
        loading("loading.t25", 25);
        auto quick = [&](const char* name, QuickHelpStart state) {
            rig.renderer.begin_frame();
            draw_quick_help_screen(rig.renderer, arc, true, state);
            px(name, {});
        };
        quick("quickhelp.rest", QuickHelpStart::Up);
        quick("quickhelp.hover", QuickHelpStart::Hover);
        quick("quickhelp.pressed", QuickHelpStart::Pressed);
        {
            ScorecardModal waiting;
            waiting.set_wide_layout(true);
            waiting.show(sample_result(), 0);
            draw_results_picture(rig, arc, waiting);
            px("results.waiting", {LayoutRect{98, 378, 389, 52}});          // (the label "Waiting for scores...": text)
            const std::vector<LayoutRect> masks = results_masks(ResultsLayout::wide(), 3);
            ScorecardModal rows;
            open_sample(rows, true);
            draw_results_picture(rig, arc, rows);
            px("results.rows", masks);
            ScorecardModal hover;
            open_sample(hover, true);
            hover.handle_mouse_motion(900, 22);
            draw_results_picture(rig, arc, hover);
            px("results.hover_leave", masks);
            ScorecardModal pressed;
            open_sample(pressed, true);
            pressed.handle_mouse_motion(900, 22);
            pressed.handle_mouse_down(900, 22);
            draw_results_picture(rig, arc, pressed);
            px("results.pressed_leave", masks);
            // numbers wider than their labels (cut at the boxes) and four teams that are not allied (four rows: the fourth row's ant hangs over the box's foot by 4 px, as in the original's page)
            sim::MatchResult big = sample_result();
            big.stats[2].friendly_lost = 1234;
            big.stats[3].new_hatched = 987;
            ScorecardModal wide_big;
            wide_big.set_wide_layout(true);
            wide_big.show(big, 3);
            wide_big.update(0.3f);
            draw_results_picture(rig, arc, wide_big);
            px("results.big_numbers", masks);
            sim::MatchResult apart = sample_result();
            apart.ally = {sim::ALLIANCE_NONE, sim::ALLIANCE_NONE, sim::ALLIANCE_NONE, sim::ALLIANCE_NONE};
            apart.decide_winners();
            ScorecardModal four;
            four.set_wide_layout(true);
            four.show(apart, 1);
            four.update(0.3f);
            check(four.rows().size() == 4, "four teams that are not allied have four rows");
            draw_results_picture(rig, arc, four);
            px("results.four_rows", results_masks(ResultsLayout::wide(), 4));
        }
        for (size_t i = 0; i < 6; ++i) {
            const StartMenu menu = make_menu(kPanels[i], true);
            draw_menu_picture(rig, arc, menu);
            px(std::string(kPanelNames[i]), menu_masks(menu));
        }
        // the panels with a notice (a dark box with a line of text in the middle group): a join that failed, a host attempt that failed (from the Host panel's own Host button), the first panel after
        // a lost game (their text masked)
        {
            StartMenu join = make_menu(MenuPanel::Connecting, true);                      // (a join in progress ...)
            join.connection_failed("The room abc does not exist on this server.");        // ... that failed: the Join panel with its notice
            check(join.panel() == MenuPanel::Join && !join.message().empty(), "a failed join is back on the Join panel with its line");
            draw_menu_picture(rig, arc, join);
            px("menu_join.notice", menu_masks(join));
            StartMenu host = make_menu(MenuPanel::Host, true);
            MenuElement go;
            if (host.find_element(MenuId::Host, go)) {
                const int32_t x = go.rect.x + go.rect.w / 2;
                const int32_t y = go.rect.y + go.rect.h / 2;
                host.on_mouse_move(x, y);
                host.on_mouse_down(x, y, SDL_BUTTON_LEFT);
                host.on_mouse_up(x, y, SDL_BUTTON_LEFT);                                  // Host: the attempt begins (the Connecting panel) ...
            }
            check(host.panel() == MenuPanel::Connecting, "the Host button starts an attempt");
            host.connection_failed("Cannot reach the server. Check the server's address and your connection.");     // ... and fails
            check(host.panel() == MenuPanel::Host && !host.message().empty(), "a failed host attempt is back on the Host panel with its line");
            draw_menu_picture(rig, arc, host);
            px("menu_host.notice", menu_masks(host));
            StartMenu main = make_menu(MenuPanel::Main, true);
            main.show_main("The connection to the other players was lost.");
            draw_menu_picture(rig, arc, main);
            px("menu_main.notice", menu_masks(main));
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
        if (std::strcmp(argv[i], "--save") == 0 && i + 1 < argc) g_save_dir = argv[++i];
    }
    ensure_sdl();
    assets::AssetArchive archive;
    if (!archive.load_from_file(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd")) {
        std::fprintf(stderr, "cannot load ants.chd\n");
        return 2;
    }
    test_layout(archive);
    test_seams(archive);
    test_boxes(archive);
    test_loading_order(archive);
    test_mockups(archive);
    test_counters(archive);
    test_pointer(archive);
    test_application(archive);
    test_fingerprints(archive);
    if (g_print) {
        std::printf("\nconstexpr Golden kGolden[] = {\n");
        for (const Measured& m : g_measured) std::printf("    {\"%s\", %s, %lluull},\n", m.name.c_str(), hex64(m.hash).c_str(), static_cast<unsigned long long>(m.count));
        std::printf("};\n");
    }
    std::printf("\nwide pages: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
