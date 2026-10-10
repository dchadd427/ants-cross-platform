// The on-screen part of the game mode 187 (docs/GAMEPLAY.md "187", include/ants_app/results_layout.hpp, scorecard.hpp, hud.hpp): the pictures the owner approved, as numbers.
//   1. the headline: "<Name> is the last colony standing!", "<A> and <B> are the last colonies standing!" (two allies that are both left), "Time is up. The most kills wins!", "Nobody is left.
//      The most kills wins!", and none for a match that a quit ended; the names are the page's own (the players' names, else the colour words);
//   2. the rows: the numbers are the kills (the score), the ants lost and the ants left, three of them; the order of the rows, "Winner!", "other players..." and the cue are the original's;
//   3. the baked headings (newstats.bmp) are not drawn in 187, in the wide page and in the classic page, and everything else of the page is (the original's modes draw them, as before);
//   4. the three headings, their strokes and arrows, the numbers and the headline stand where the approved picture has them (the positions of tools/compose.py at 960 x 540, written out here
//      as numbers of their own: the arrows pixel for pixel, the texts by the calls that draw them), and nothing else of the page changed (pixel for pixel, outside the places that are 187's);
//      the lettering has the look of the original's own (YOUR SCORE, Winner!): the fill, a lit edge one pixel up and left, a shadow edge one pixel down and right, in the colours sampled from the art;
//      the page of the original's rules is as it was (its draw calls and its pixels, pinned from before the lettering had that look);
//   5. the headline fits: at the real fonts every wording of the page lies inside its box, wrapped or made smaller when a name is long, and never touches the art around it;
//   6. the start line of the chat log ("Game started! Most kills wins.") and the line "<Name> (<Colour>) is out of ants!" reach the chat log; the original's modes say the old start line and
//      nothing of ants running out;
//   7. the application: a match of the rules 187 starts with its line and ends in its results page.
// Usage: test_results_187. Exit code 0 when every check passes.
#include <SDL.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "ants_app/application.hpp"
#include "ants_app/hud.hpp"
#include "ants_app/renderer.hpp"
#include "ants_app/results_layout.hpp"
#include "ants_app/scorecard.hpp"
#include "ants_app/text_layout.hpp"
#include "ants_assets/asset_archive.hpp"
#include "ants_sim/game_mode.hpp"
#include "ants_sim/game_strings.hpp"
#include "ants_sim/match_stats.hpp"
#include "ants_sim/sim_engine.hpp"
#include "ants_test_paths.hpp"

using namespace ants;
using namespace ants::app;

namespace {

int g_checks = 0;
int g_failures = 0;
const char* g_group = "";
bool g_print = false;                    // --print writes the pinned numbers of test_original_page (for regeneration; never to silence a failure)

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

std::string str(int64_t v) { return std::to_string(v); }

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

std::string hex64(uint64_t v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%016llxull", static_cast<unsigned long long>(v));
    return buf;
}

void ensure_sdl() {
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);                      // nothing is shown or heard
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
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

// =====================================================================================================================================================
// The recording renderer, the real renderer, pixels
// =====================================================================================================================================================

/// Records the calls in order (the deterministic measures of a text: 6 pixels a character, the cell height of the font)
class Spy : public IRenderer {
public:
    enum class Kind : uint8_t { Sprite, Region, Named, Fill, Rect, Text, Squeezed, Clip, ClearClip, Origin };
    struct Ev {
        Kind kind{Kind::Sprite};
        std::string name;                     // the sprite's name, the text
        int32_t x{0}, y{0}, w{0}, h{0};       // (a squeezed text: w is the width it is squeezed to)
        assets::ColorRGBA colour{};
        FontSize size{FontSize::Px12};
    };

    explicit Spy(const assets::AssetArchive& archive) : arc_(archive) {}

    void draw_sprite(uint32_t id, int32_t x, int32_t y, bool) override { push(Kind::Sprite, arc_.get_sprite(id).name, x, y); }
    void draw_named_sprite(const std::string& name, int32_t x, int32_t y, bool) override { push(Kind::Named, name, x, y); }
    void draw_sprite_region(uint32_t id, int32_t x, int32_t y, int32_t w, int32_t h, int32_t, int32_t, int32_t, int32_t) override {
        Ev e = base(Kind::Region);
        e.name = arc_.get_sprite(id).name;
        e.x = x; e.y = y; e.w = w; e.h = h;
        events.push_back(e);
    }
    void fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, assets::ColorRGBA c) override { rect(Kind::Fill, x, y, w, h, c); }
    void draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, assets::ColorRGBA c) override { rect(Kind::Rect, x, y, w, h, c); }
    void draw_text(const std::string& s, int32_t x, int32_t y, assets::ColorRGBA c) override { text(Kind::Text, s, x, y, c, FontSize::Px12, 0); }
    void draw_text(const std::string& s, int32_t x, int32_t y, assets::ColorRGBA c, FontSize size) override { text(Kind::Text, s, x, y, c, size, 0); }
    void draw_text_squeezed(const std::string& s, int32_t x, int32_t y, assets::ColorRGBA c, FontSize size, int32_t max_width) override { text(Kind::Squeezed, s, x, y, c, size, max_width); }
    int32_t get_text_width(const std::string& s, FontSize = FontSize::Px12) const override { return static_cast<int32_t>(s.size()) * 6; }
    int32_t get_text_height(FontSize size = FontSize::Px12) const override { return font_cell_height(size); }
    void set_hud_team(uint8_t) override {}
    void set_clip_rect(int32_t x, int32_t y, int32_t w, int32_t h) override { rect(Kind::Clip, x, y, w, h, assets::ColorRGBA{}); }
    void clear_clip_rect() override { events.push_back(base(Kind::ClearClip)); }
    void set_origin(int32_t x, int32_t y) override {
        Ev e = base(Kind::Origin);
        e.x = x; e.y = y;
        events.push_back(e);
    }

    std::vector<Ev> events;

    std::vector<const Ev*> sprites() const {
        std::vector<const Ev*> out;
        for (const Ev& e : events) {
            if (e.kind == Kind::Sprite || e.kind == Kind::Named || e.kind == Kind::Region) out.push_back(&e);
        }
        return out;
    }
    bool draws_sprite(const std::string& name) const {
        for (const Ev* e : sprites()) {
            if (e->name == name) return true;
        }
        return false;
    }
    const Ev* find_text(const std::string& s) const {
        for (const Ev& e : events) {
            if ((e.kind == Kind::Text || e.kind == Kind::Squeezed) && e.name == s) return &e;
        }
        return nullptr;
    }
    size_t count(Kind k) const {
        size_t n = 0;
        for (const Ev& e : events) n += e.kind == k ? 1u : 0u;
        return n;
    }
    uint64_t fingerprint() const {
        Fnv f;
        for (const Ev& e : events) {
            f.byte(static_cast<uint8_t>(e.kind));
            f.text(e.name);
            for (int32_t v : {e.x, e.y, e.w, e.h}) f.i32(v);
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
    void rect(Kind k, int32_t x, int32_t y, int32_t w, int32_t h, assets::ColorRGBA c) {
        Ev e = base(k);
        e.x = x; e.y = y; e.w = w; e.h = h; e.colour = c;
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

struct RendererRig {
    RendererRig(const assets::AssetArchive& archive, int32_t w, int32_t h) : width(w), height(h) {
        const QuietStdout quiet;
        ensure_sdl();
        win = SDL_CreateWindow("results-187", 0, 0, w, h, SDL_WINDOW_HIDDEN);
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

// The look of the original's own lettering ("YOUR SCORE", "Winner!"), sampled from its art (tools/compose.py: ARTGREEN, ARTHI, ARTLO): the fill, the lit edge one pixel up and to the left, the shadow
// edge one pixel down and to the right
constexpr Rgb kFill{43, 95, 67};
constexpr Rgb kLit{115, 191, 155};
constexpr Rgb kShadow{19, 55, 47};

bool has_colour(const Spy::Ev& e, const Rgb& c) { return e.colour.r == c.r && e.colour.g == c.g && e.colour.b == c.b && e.colour.a == 255; }

/// The three copies that a text of this look is drawn in, in the order they are drawn: the shadow copy (at +1, +1), the lit copy (at -1, -1), then the fill (at the true place): three text calls in a
/// row for the same text, in these colours
struct ArtText {
    const Spy::Ev* shadow{nullptr};
    const Spy::Ev* lit{nullptr};
    const Spy::Ev* fill{nullptr};
};

ArtText find_art_text(const Spy& spy, const std::string& text) {
    for (size_t i = 0; i + 2 < spy.events.size(); ++i) {
        const Spy::Ev& a = spy.events[i];
        const Spy::Ev& b = spy.events[i + 1];
        const Spy::Ev& c = spy.events[i + 2];
        if (a.kind != Spy::Kind::Text || b.kind != Spy::Kind::Text || c.kind != Spy::Kind::Text || a.name != text || b.name != text || c.name != text) continue;
        if (has_colour(a, kShadow) && has_colour(b, kLit) && has_colour(c, kFill)) return ArtText{&a, &b, &c};
    }
    return ArtText{};
}

/// The copies lie as the look wants: the shadow 1 px right and down, the lit 1 px left and up of the fill, all of one size
bool art_text_in_place(const ArtText& t) {
    return t.fill != nullptr && t.shadow->x == t.fill->x + 1 && t.shadow->y == t.fill->y + 1 && t.lit->x == t.fill->x - 1 && t.lit->y == t.fill->y - 1 && t.shadow->size == t.fill->size && t.lit->size == t.fill->size;
}

// =====================================================================================================================================================
// The results that the tests hand to the page
// =====================================================================================================================================================

const std::array<std::string, 4> kNames = {"Juniper", "Odile", "Marta", "Theo"};

struct Scene {
    uint8_t present{0x0F};
    std::array<int32_t, 4> kills{9, 5, 8, 4};
    std::array<uint32_t, 4> lost{5, 8, 6, 8};
    std::array<uint32_t, 4> left{1, 0, 0, 0};
    uint8_t standing{0};
    uint16_t quitter{sim::NO_QUITTER};
    std::array<uint8_t, 4> ally{sim::ALLIANCE_NONE, sim::ALLIANCE_NONE, sim::ALLIANCE_NONE, sim::ALLIANCE_NONE};
};

sim::MatchResult make_result(const Scene& s) {
    sim::MatchResult mr{};
    mr.is_over = true;
    mr.present_mask = s.present;
    mr.ally = s.ally;
    mr.quitter = s.quitter;
    mr.standing = s.standing;
    for (size_t p = 0; p < 4; ++p) {
        mr.stats[p].score = s.kills[p];
        mr.stats[p].friendly_lost = s.lost[p];
        mr.stats[p].enemy_killed = static_cast<uint32_t>(s.kills[p]);
        mr.stats[p].new_hatched = 3;                                     // (what the original's page shows in its fourth column)
        mr.ants_left[p] = s.left[p];
    }
    mr.decide_winners();
    return mr;
}

/// The page, open and with its rows, for the local team 0 ("Juniper") in the given rules
void open_page(ScorecardModal& card, const Scene& s, sim::GameMode mode, bool wide) {
    card.set_wide_layout(wide);
    card.set_player_names(kNames);
    card.show(make_result(s), 0, mode);
    card.update(0.3f);
}

std::string headline_of(const Scene& s, sim::GameMode mode = sim::GameMode::Kills187) {
    ScorecardModal card;
    open_page(card, s, mode, true);
    return card.headline();
}

Scene allies_scene() {                      // Juniper and Odile are allies, Marta and Theo are not
    Scene s;
    s.ally = {1, 0, sim::ALLIANCE_NONE, sim::ALLIANCE_NONE};
    return s;
}

// =====================================================================================================================================================
// 1. The headline
// =====================================================================================================================================================

void test_headline() {
    group("headline", "the words of the line at the top of the page, for every way a match ends");
    {   // one colony stands, the page's own names: the name of the team that stands, not of the local one
        Scene s;
        check(headline_of(s) == "Juniper is the last colony standing!", "one team stands (the local one): \"" + headline_of(s) + "\"");
        s.standing = 2;
        s.left = {0, 0, 4, 0};
        check(headline_of(s) == "Marta is the last colony standing!", "one team stands (another one): \"" + headline_of(s) + "\"");
        s.standing = 3;
        s.left = {0, 0, 0, 12};
        check(headline_of(s) == "Theo is the last colony standing!", "the last team stands: \"" + headline_of(s) + "\"");
    }
    {   // two allies that both have ants: both are named, the lower team first, whichever of them is the one that stands
        Scene s = allies_scene();
        s.left = {3, 2, 0, 0};
        s.standing = 0;
        check(headline_of(s) == "Juniper and Odile are the last colonies standing!", "two allies are left: \"" + headline_of(s) + "\"");
        s.standing = 1;
        check(headline_of(s) == "Juniper and Odile are the last colonies standing!", "... whichever of them the engine names: \"" + headline_of(s) + "\"");
        s.left = {0, 2, 0, 0};
        check(headline_of(s) == "Odile is the last colony standing!", "an ally that has no ant left is not named: \"" + headline_of(s) + "\"");
        s.left = {3, 0, 0, 0};
        s.standing = 0;
        check(headline_of(s) == "Juniper is the last colony standing!", "... the other way round: \"" + headline_of(s) + "\"");
    }
    {   // an alliance that the other side does not return is two teams (the rows of the page are the confirmed ones)
        Scene s;
        s.ally = {1, sim::ALLIANCE_NONE, sim::ALLIANCE_NONE, sim::ALLIANCE_NONE};
        s.left = {3, 2, 0, 0};
        s.standing = 0;
        check(headline_of(s) == "Juniper is the last colony standing!", "one-sided alliance, one colony: \"" + headline_of(s) + "\"");
    }
    {   // the clock ran out: nobody stands; ants are left somewhere
        Scene s;
        s.standing = sim::PLAYER_NEUTRAL;
        s.left = {1, 4, 0, 1};
        check(headline_of(s) == "Time is up. The most kills wins!", "the clock ran out: \"" + headline_of(s) + "\"");
        // ... nobody has an ant left: nobody is left
        s.left = {0, 0, 0, 0};
        check(headline_of(s) == "Nobody is left. The most kills wins!", "no team has an ant: \"" + headline_of(s) + "\"");
        // an ant of a team that has no row does not count (a team that is not in the match, or dropped)
        s.present = 0x07;
        s.left = {0, 0, 0, 9};
        check(headline_of(s) == "Nobody is left. The most kills wins!", "an ant of a team without a row does not count: \"" + headline_of(s) + "\"");
        s.left = {0, 0, 2, 9};
        check(headline_of(s) == "Time is up. The most kills wins!", "an ant of a team with a row does: \"" + headline_of(s) + "\"");
    }
    {   // a player's quit ended the match: no wording was approved for it, so the page has no headline
        Scene s;
        s.standing = sim::PLAYER_NEUTRAL;
        s.quitter = 2;
        s.left = {2, 2, 2, 2};
        check(headline_of(s).empty(), "a quit ended the match: no headline");
    }
    {   // not before the rows are built, and not in the original's rules
        ScorecardModal card;
        card.set_player_names(kNames);
        card.show(make_result(Scene{}), 0, sim::GameMode::Kills187);
        check(card.headline().empty() && card.is_waiting(), "the page that waits for the scores has no headline");
        card.update(0.3f);
        check(card.headline() == "Juniper is the last colony standing!", "... the rows bring it");
        card.show(make_result(Scene{}), 0, sim::GameMode::HighestScore);
        check(card.headline().empty(), "a page that is opened again in the original's rules has none");
        card.update(0.3f);
        check(card.headline().empty() && card.game_mode() == sim::GameMode::HighestScore, "... and the rows do not bring one");
        check(headline_of(Scene{}, sim::GameMode::HighestScore).empty(), "the original's rules: no headline");
    }
    {   // the colour words stand in for names that are not given (the page's own display names): the headline names what the first row names
        ScorecardModal card;
        card.set_wide_layout(true);
        card.show(make_result(Scene{}), 0, sim::GameMode::Kills187);
        card.update(0.3f);
        const std::string tail = " is the last colony standing!";
        check(card.rows().size() == 4 && card.headline() == card.rows()[0].name + tail && !card.rows()[0].name.empty(), "without a name the colour word is the name, as on the first row: \"" + card.headline() + "\"");
    }
}

// =====================================================================================================================================================
// 2. The rows
// =====================================================================================================================================================

void test_rows() {
    group("rows", "three numbers a row (kills, ants lost, ants left), the original's order, and the original's modes with their four");
    for (const bool wide : {true, false}) {
        const std::string at = std::string(wide ? "wide" : "classic") + ": ";
        Scene s;                                                  // Juniper stands with 9 kills; Marta has 8, Odile 5, Theo 4
        ScorecardModal card;
        open_page(card, s, sim::GameMode::Kills187, wide);
        const std::vector<ScorecardModal::Row>& rows = card.rows();
        check(rows.size() == 4, at + "four rows");
        if (rows.size() != 4) continue;
        check(rows[0].name == "Juniper" && rows[1].name == "Marta" && rows[2].name == "Odile" && rows[3].name == "Theo", at + "the last colony first, the others by their kills: " + rows[0].name + ", " + rows[1].name + ", " + rows[2].name + ", " + rows[3].name);
        check(rows[0].numbers[0] == "9" && rows[0].numbers[1] == "5" && rows[0].numbers[2] == "1" && rows[0].numbers[3].empty(), at + "the first row: kills 9, ants lost 5, ants left 1, and nothing in a fourth place");
        check(rows[1].numbers[0] == "8" && rows[1].numbers[1] == "6" && rows[1].numbers[2] == "0", at + "Marta: 8, 6, 0");
        check(rows[3].numbers[0] == "4" && rows[3].numbers[1] == "8" && rows[3].numbers[2] == "0", at + "Theo: 4, 8, 0");
    }
    {   // the last one standing wins outright, although another team has more kills
        Scene s;
        s.kills = {3, 12, 2, 1};
        ScorecardModal card;
        open_page(card, s, sim::GameMode::Kills187, true);
        check(card.rows().size() == 4 && card.rows()[0].name == "Juniper" && card.rows()[1].name == "Odile", "the last colony is first although Odile has more kills");
        const sim::MatchResult mr = make_result(s);
        check(mr.is_winner(0) && !mr.is_winner(1), "and the cue is the winner's for it only");
    }
    {   // the clock: the order of the kills, ties for the local team
        Scene s;
        s.standing = sim::PLAYER_NEUTRAL;
        s.kills = {7, 7, 9, 1};
        s.left = {2, 2, 2, 2};
        ScorecardModal card;
        open_page(card, s, sim::GameMode::Kills187, true);
        check(card.rows().size() == 4 && card.rows()[0].name == "Marta" && card.rows()[1].name == "Juniper" && card.rows()[2].name == "Odile" && card.rows()[3].name == "Theo", "the clock: by kills, the tie to the local team");
        check(card.rows().size() == 4 && card.rows()[0].numbers[2] == "2", "ants left of a row on the clock page");
    }
    {   // allies share a row: the numbers are added up
        Scene s = allies_scene();
        s.left = {3, 2, 0, 0};
        ScorecardModal card;
        open_page(card, s, sim::GameMode::Kills187, true);
        check(card.rows().size() == 3 && card.rows()[0].name == "Juniper & Odile", "two allies share the first row");
        check(card.rows().size() == 3 && card.rows()[0].numbers[0] == "14" && card.rows()[0].numbers[1] == "13" && card.rows()[0].numbers[2] == "5", "the row adds them up: kills 9 + 5, lost 5 + 8, left 3 + 2");
    }
    {   // the original's modes keep their four numbers (score, friendly lost, enemy killed, new hatched)
        Scene s;
        ScorecardModal card;
        open_page(card, s, sim::GameMode::HighestScore, true);
        check(card.rows().size() == 4 && card.rows()[0].numbers[0] == "9" && card.rows()[0].numbers[1] == "5" && card.rows()[0].numbers[2] == "9" && card.rows()[0].numbers[3] == "3",
              "the original's rules: score, friendly lost, enemy killed, new hatched");
    }
}

// =====================================================================================================================================================
// 3. The baked headings
// =====================================================================================================================================================

void test_baked_headings(const assets::AssetArchive& arc) {
    group("headers", "newstats.bmp (the four baked headings) is not drawn in 187, in either page; nothing else of the page's pictures goes");
    for (const bool wide : {true, false}) {
        const std::string at = std::string(wide ? "wide" : "classic") + ": ";
        Scene s;
        ScorecardModal original, mode187;
        open_page(original, s, sim::GameMode::HighestScore, wide);
        open_page(mode187, s, sim::GameMode::Kills187, wide);
        Spy a(arc), b(arc);
        original.render(a, arc);
        mode187.render(b, arc);
        check(a.draws_sprite("newstats.bmp"), at + "the original's rules draw the four headings");
        check(!b.draws_sprite("newstats.bmp"), at + "187 does not");
        // the sprites of 187 are the original's less newstats.bmp, in the same order at the same places
        std::vector<const Spy::Ev*> want, got = b.sprites();
        for (const Spy::Ev* e : a.sprites()) {
            if (e->name != "newstats.bmp") want.push_back(e);
        }
        bool same = want.size() == got.size();
        for (size_t i = 0; same && i < want.size(); ++i) same = want[i]->name == got[i]->name && want[i]->kind == got[i]->kind && want[i]->x == got[i]->x && want[i]->y == got[i]->y && want[i]->w == got[i]->w && want[i]->h == got[i]->h;
        check(same, at + "every other picture is drawn as in the original's page, in the same order (" + str(static_cast<int64_t>(want.size())) + " of " + str(static_cast<int64_t>(a.sprites().size())) + ")");
        check(a.find_text("Kills") == nullptr && a.find_text("Ants lost") == nullptr && a.find_text("Ants left") == nullptr, at + "the original's page has none of 187's headings");
        check(b.find_text("Kills") != nullptr && b.find_text("Ants lost") != nullptr && b.find_text("Ants left") != nullptr, at + "187's page has its three");
        check(b.find_text("Score") == nullptr && b.find_text("Friendly Ants Lost") == nullptr && b.find_text("New Ants Hatched") == nullptr, at + "and none of the baked ones as text");
    }
}

// =====================================================================================================================================================
// 4. Where everything stands
// =====================================================================================================================================================

// The approved picture's positions, from tools/compose.py (results_page, a 960 x 540 frame), as numbers of their own: the three columns the arrows stand on, the text's right edge (the column - 14),
// the row of each stroke (the heading's top + its size / 2 + 2), the end of the shafts, the head's rows, the line of the headline (centred on x 570, its text 35 px high at y 71)
constexpr int32_t kPictureColumn[3] = {805, 854, 896};
constexpr int32_t kPictureStrokeY[3] = {220, 185, 153};      // 204 + 28 / 2 + 2, 172 + 22 / 2 + 2, 140 + 22 / 2 + 2
constexpr int32_t kPictureShaftEnd = 246;                    // the shafts run down to this row, the heads are the polygon (cx + 1, 238), (cx + 11, 238), (cx + 6, 247)
constexpr int32_t kPictureHeadTop = 238;
constexpr int32_t kPictureTip = 247;
constexpr int32_t kPictureHeadlineCentre = 570;
constexpr int32_t kPictureHeadlineY = 71;

void test_positions(const assets::AssetArchive& arc) {
    group("places", "the three headings, strokes, arrows, numbers and the headline of the wide page stand where the approved picture has them; the classic page moves them as the page moves");
    const ResultsLayout& w = ResultsLayout::wide();
    const ResultsLayout& c = ResultsLayout::classic();
    // the layout's own numbers
    for (size_t i = 0; i < 3; ++i) {
        const int32_t col = kPictureColumn[i];
        const std::string n = str(static_cast<int64_t>(i));
        check(w.numbers187_x[i] == col, "wide: the numbers' column " + n + " is at " + str(col) + " (" + str(w.numbers187_x[i]) + ")");
        check(w.column_x[static_cast<size_t>(w.headings187[i].column)] == col, "wide: the heading " + n + "'s arrow stands on the column " + str(col));
        check(w.headings187[i].text_right == col - 14, "wide: its text ends at " + str(col - 14) + " (" + str(w.headings187[i].text_right) + ")");
        check(w.headings187[i].stroke_y == kPictureStrokeY[i], "wide: its stroke is at row " + str(kPictureStrokeY[i]) + " (" + str(w.headings187[i].stroke_y) + ")");
        // the classic page: what the page moves by 320 and 30 (the layout's own dx and dy)
        check(c.numbers187_x[i] + w.dx == w.numbers187_x[i] && c.headings187[i].text_right + w.dx == w.headings187[i].text_right, "classic: columns and texts are the wide ones less " + str(w.dx));
        check(c.headings187[i].stroke_y + w.dy == w.headings187[i].stroke_y && c.headings187[i].text_y + w.dy == w.headings187[i].text_y, "classic: rows are the wide ones less " + str(w.dy));
    }
    check(w.arrow_tip_y187 == kPictureTip && c.arrow_tip_y187 + w.dy == kPictureTip, "the arrows' tips are at row 247 (wide)");
    check(w.headline187.x + w.headline187.w / 2 == kPictureHeadlineCentre, "the headline is centred on x 570");
    check(w.headline187.y + (w.headline187.h - font_cell_height(FontSize::Px35)) / 2 == kPictureHeadlineY, "its 35 px line is at y 71");
    check(w.numbers187_w[0] == 49 && w.numbers187_w[1] == 42 && w.numbers187_w[2] == 24, "the labels of the three numbers: up to the next column, the last one up to the box's inside edge");
    // the headings' sizes: the game's own letters, the first (the lowest) larger
    check(w.headings187[0].size == FontSize::Px35 && w.headings187[1].size == FontSize::Px27 && w.headings187[2].size == FontSize::Px27, "the sizes of the three headings: 35, 27, 27");

    // the calls that draw the page
    for (const bool wide : {true, false}) {
        const ResultsLayout& l = ResultsLayout::of(wide);
        const std::string at = std::string(wide ? "wide" : "classic") + ": ";
        Scene s;
        ScorecardModal card;
        open_page(card, s, sim::GameMode::Kills187, wide);
        Spy spy(arc);
        card.render(spy, arc);
        const char* const texts[3] = {"Kills", "Ants lost", "Ants left"};
        for (size_t i = 0; i < 3; ++i) {
            const ArtText t = find_art_text(spy, texts[i]);
            check(t.fill != nullptr, at + "\"" + texts[i] + "\" is drawn in three copies: the shadow, the lit edge, the fill");
            if (t.fill == nullptr) continue;
            const Spy::Ev* e = t.fill;
            const int32_t width = static_cast<int32_t>(std::string(texts[i]).size()) * 6;
            check(e->x + width == l.headings187[i].text_right && e->y == l.headings187[i].text_y, at + "\"" + texts[i] + "\" is right aligned at " + str(l.headings187[i].text_right) + ", its cell's top at " + str(l.headings187[i].text_y) + " (" + str(e->x) + " + " + str(width) + ", " + str(e->y) + ")");
            check(e->size == l.headings187[i].size, at + "\"" + texts[i] + "\" is at its size");
            check(art_text_in_place(t), at + "\"" + texts[i] + "\": the shadow copy is 1 px right and down, the lit copy 1 px left and up of the fill");
        }
        // the three numbers of every row: a clipped, squeezed label at the column, the row's y, nothing in a fourth place
        const char* const numbers[4][3] = {{"9", "5", "1"}, {"8", "6", "0"}, {"5", "8", "0"}, {"4", "8", "0"}};
        int found = 0, bracketed = 0;
        for (size_t row = 0; row < 4; ++row) {
            for (size_t col = 0; col < 3; ++col) {
                for (size_t i = 0; i < spy.events.size(); ++i) {
                    const Spy::Ev& e = spy.events[i];
                    if (e.kind != Spy::Kind::Squeezed || e.name != numbers[row][col] || e.x != l.numbers187_x[col] || e.y != l.row_y(row)) continue;
                    ++found;
                    const bool clip_before = i > 0 && spy.events[i - 1].kind == Spy::Kind::Clip && spy.events[i - 1].x == l.numbers187_x[col] && spy.events[i - 1].y == l.row_y(row) && spy.events[i - 1].w == l.numbers187_w[col];
                    const bool clear_after = i + 1 < spy.events.size() && spy.events[i + 1].kind == Spy::Kind::ClearClip;
                    if (clip_before && clear_after && e.size == FontSize::Px18 && e.w == kResultsDigitWidth) ++bracketed;
                    break;
                }
            }
        }
        check(found == 12 && bracketed == 12, at + "the twelve numbers (kills, ants lost, ants left of four rows) are drawn in the columns of the headings, clipped, in the original's 8 px digits (" + str(found) + ", " + str(bracketed) + ")");
        check(spy.count(Spy::Kind::Squeezed) == 12, at + "and there are no others");
        // the headline
        const std::string headline = "Juniper is the last colony standing!";
        const ArtText t = find_art_text(spy, headline);
        check(t.fill != nullptr, at + "the headline is drawn in three copies: the shadow, the lit edge, the fill");
        if (t.fill != nullptr) {
            const Spy::Ev* h = t.fill;
            const int32_t width = static_cast<int32_t>(headline.size()) * 6;
            check(h->size == FontSize::Px35 && h->x == l.headline187.x + (l.headline187.w - width) / 2 && h->y == l.headline187.y + (l.headline187.h - 35) / 2, at + "centred in its box, one 35 px line (" + str(h->x) + ", " + str(h->y) + ")");
            check(art_text_in_place(t), at + "the shadow copy is 1 px right and down, the lit copy 1 px left and up of the fill");
        }
    }
}

// =====================================================================================================================================================
// 4b. Pixels: the arrows, and nothing else changed
// =====================================================================================================================================================

void test_pixels(const assets::AssetArchive& arc) {
    group("pixels", "the arrows pixel for pixel; everything outside 187's own places is the original's page, pixel for pixel; the headline never touches the art");
    RendererRig rig(arc, 960, 540);
    check(rig.ok, "the renderer is up on a 960 x 540 canvas");
    if (!rig.ok) return;
    const ResultsLayout& l = ResultsLayout::wide();
    Scene s;
    ScorecardModal original, mode187, quiet187;
    open_page(original, s, sim::GameMode::HighestScore, true);
    open_page(mode187, s, sim::GameMode::Kills187, true);
    Scene quit = s;
    quit.standing = sim::PLAYER_NEUTRAL;
    quit.quitter = 3;                                                // (the page of a quit has no headline, the rest of the page is the same kind of page)
    open_page(quiet187, quit, sim::GameMode::Kills187, true);
    check(!mode187.headline().empty() && quiet187.headline().empty(), "the pages to compare: one with a headline, one without");

    rig.renderer.begin_frame();
    original.render(rig.renderer, arc);
    const std::vector<uint8_t> was = rig.read();
    rig.renderer.begin_frame();
    mode187.render(rig.renderer, arc);
    const std::vector<uint8_t> now = rig.read();
    rig.renderer.begin_frame();
    quiet187.render(rig.renderer, arc);
    const std::vector<uint8_t> bare = rig.read();
    rig.renderer.begin_frame();
    draw_results_art(rig.renderer, arc, false);                      // the ground of the 187 page: the art without the baked headings and without any lettering of 187's
    const std::vector<uint8_t> ground = rig.read();

    // the arrows, as the picture draws them (tools/compose.py: emboss_line, then emboss_poly, per heading): a 2 px stroke from 12 px left of the column to the shaft and the shaft (2 px wide at the
    // column + 6) down to row 238, as one shape; the head, the polygon (cx + 1, 238), (cx + 11, 238), (cx + 6, 247), as the next; a shape is its shadow copy (1 px right and down), its lit copy (1 px left
    // and up) and then its fill. The pixels around every arrow, from the stroke's start to the head's end and a pixel beyond (the shadow's last row, 248, lies in the first row of the winner's
    // box), are these three colours where the picture has them and the page's ground (the art without the baked headings) where it has not
    const int32_t half[10] = {5, 4, 4, 3, 3, 2, 2, 1, 1, 0};                  // the head's rows (238 .. 247): half widths around the shaft's left column
    for (size_t i = 0; i < 3; ++i) {
        const int32_t cx = kPictureColumn[i];
        const int32_t y0 = kPictureStrokeY[i];
        const std::string n = "arrow " + str(static_cast<int64_t>(i)) + ": ";
        std::vector<int8_t> paint(960u * 540u, 0);                          // 0 nothing, 1 shadow, 2 lit, 3 fill
        auto put = [&](int32_t x, int32_t y, int8_t what) { paint[static_cast<size_t>(y) * 960u + static_cast<size_t>(x)] = what; };
        auto shape = [&](const std::vector<LayoutRect>& parts) {
            const struct { int32_t d; int8_t what; } passes[3] = {{1, 1}, {-1, 2}, {0, 3}};
            for (const auto& pass : passes) {
                for (const LayoutRect& r : parts) {
                    for (int32_t y = r.y; y < r.bottom(); ++y) {
                        for (int32_t x = r.x; x < r.right(); ++x) put(x + pass.d, y + pass.d, pass.what);
                    }
                }
            }
        };
        shape({LayoutRect{cx - 12, y0, 20, 2}, LayoutRect{cx + 6, y0, 2, kPictureHeadTop - y0 + 1}});
        std::vector<LayoutRect> head;
        for (int32_t k = 0; k < 10; ++k) head.push_back(LayoutRect{cx + 6 - half[k], kPictureHeadTop + k, 2 * half[k] + 1, 1});
        shape(head);
        int64_t wrong = 0, painted = 0;
        int32_t first_x = -1, first_y = -1;
        for (int32_t y = y0 - 1; y <= kPictureTip + 1; ++y) {
            for (int32_t x = cx - 11; x <= cx + 9; ++x) {                    // (the heading's own text ends left of this: its shadow lies at most at the stroke's lit edge, x - 13)
                const int8_t what = paint[static_cast<size_t>(y) * 960u + static_cast<size_t>(x)];
                const Rgb want = what == 1 ? kShadow : what == 2 ? kLit : what == 3 ? kFill : pixel(ground, 960, x, y);
                painted += what != 0 ? 1 : 0;
                if (pixel(now, 960, x, y) != want) {
                    if (wrong == 0) { first_x = x; first_y = y; }
                    ++wrong;
                }
            }
        }
        check(painted > 150, n + "the picture's arrow has " + str(painted) + " pixels around it");
        check(wrong == 0, n + "stroke, shaft and head are the picture's, with the lit and the shadow edge, pixel for pixel (" + str(wrong) + " differ, the first at (" + str(first_x) + ", " + str(first_y) + "))");
    }
    // the three colours are in the right places by their own: the stroke's fill, the lit edge over it, the shadow edge under it (the first arrow's stroke at row 220, columns 800)
    check(pixel(now, 960, 800, 220) == kFill && pixel(now, 960, 800, 221) == kFill && pixel(now, 960, 800, 219) == kLit && pixel(now, 960, 800, 222) == kShadow, "the stroke: fill on rows 220 and 221, the lit edge above (219), the shadow edge below (222)");
    check(pixel(now, 960, 811, 230) == kFill && pixel(now, 960, 812, 230) == kFill && pixel(now, 960, 810, 230) == kLit && pixel(now, 960, 813, 230) == kShadow, "the shaft: fill at x 811 and 812, the lit edge left of it (810), the shadow edge right of it (813)");
    check(pixel(ground, 960, 800, 219) != kLit && pixel(ground, 960, 800, 220) != kFill && pixel(ground, 960, 811, 230) != kFill, "the ground has none of it there");

    // nothing else changed: the places that are 187's own are the baked headings' rectangle, the area of the headings and arrows, the headline's line, and the numbers of the rows
    std::vector<LayoutRect> own;
    own.push_back(l.headers);
    own.push_back(LayoutRect{680, 120, 250, 130});                                     // the headings (text, strokes, arrows) down to the row above the winner's box
    own.push_back(LayoutRect{l.headline187.x, l.headline187.y - 4, l.headline187.w, l.headline187.h + 8});
    for (size_t row = 0; row < 4; ++row) own.push_back(LayoutRect{l.column_x[0] - 2, l.row_y(row) - 2, 924 - (l.column_x[0] - 2), 22});
    int64_t outside_diff = 0, inside_diff = 0;
    int32_t first_x = -1, first_y = -1;
    for (int32_t y = 0; y < 540; ++y) {
        for (int32_t x = 0; x < 960; ++x) {
            if (pixel(was, 960, x, y) == pixel(now, 960, x, y)) continue;
            bool mine = false;
            for (const LayoutRect& r : own) mine = mine || r.contains(x, y);
            if (mine) {
                ++inside_diff;
            } else {
                if (outside_diff == 0) { first_x = x; first_y = y; }
                ++outside_diff;
            }
        }
    }
    check(outside_diff == 0, "outside 187's own places the page is the original's, pixel for pixel (" + str(outside_diff) + " pixels differ, the first at (" + str(first_x) + ", " + str(first_y) + "))");
    check(inside_diff > 1500, "inside them it is not (" + str(inside_diff) + " pixels differ)");

    // the headline does not touch the art around it: the pictures of the page are the same whether the headline is there or not (the page of a quit has none). The banner and the Leave Game
    // button: every pixel of their rectangles; YOUR SCORE: every pixel of its letters (the line's box reaches over the corner of the picture's rectangle, where there are none)
    const LayoutRect around[] = {l.banner, l.leave};
    const char* const names[] = {"the banner", "the Leave Game button"};
    for (size_t k = 0; k < 2; ++k) {
        int64_t touched = 0;
        for (int32_t y = around[k].y; y < around[k].bottom(); ++y) {
            for (int32_t x = around[k].x; x < around[k].right(); ++x) touched += pixel(now, 960, x, y) != pixel(bare, 960, x, y) ? 1 : 0;
        }
        check(touched == 0, std::string("the headline does not touch ") + names[k] + " (" + str(touched) + " pixels differ)");
    }
    {   // (the picture also has a few scattered pink pixels in its clay; the letters are its green ones)
        int64_t letters = 0, touched = 0;
        for (int32_t y = l.your_score.y; y < l.your_score.bottom(); ++y) {
            for (int32_t x = l.your_score.x; x < l.your_score.right(); ++x) {
                const Rgb art = pixel(bare, 960, x, y);
                if (art.g <= art.r) continue;
                ++letters;
                touched += pixel(now, 960, x, y) != art ? 1 : 0;
            }
        }
        check(letters > 5000 && touched == 0, "the headline does not touch the letters of YOUR SCORE (" + str(touched) + " of " + str(letters) + " green pixels differ)");
    }
    // ... and it has ink: the box of the line holds pixels of the fill
    int64_t ink = 0;
    for (int32_t y = l.headline187.y; y < l.headline187.bottom(); ++y) {
        for (int32_t x = l.headline187.x; x < l.headline187.right(); ++x) ink += pixel(now, 960, x, y) == kFill ? 1 : 0;
    }
    check(ink > 800, "the headline has ink in its box (" + str(ink) + " pixels of the fill)");

    // the look of the lettering: a pixel of the lit edge has the fill one pixel down and to the right of it (the edge is the text's own copy, moved up and left), a pixel of the shadow edge has it one pixel
    // up and to the left. The lettering of 187: the headline and the three headings (the boxes that the texts lie in; their pixels depend on the machine's font, so the check is by relation, not by place)
    struct Part {
        const char* what;
        LayoutRect box;
        int64_t fill_at_least;
        int64_t edge_at_least;                  // (an edge shows only where the text's pixel is fully covered: thin strokes are blended with the clay)
    };
    std::vector<Part> parts;
    parts.push_back({"the headline", LayoutRect{l.headline187.x, l.headline187.y - 4, l.headline187.w, l.headline187.h + 8}, 800, 30});
    for (const Results187Heading& h : l.headings187) {
        const int32_t w = rig.renderer.get_text_width(h.text, h.size);
        parts.push_back({h.text, LayoutRect{h.text_right - w - 2, h.text_y - 2, w + 4, font_cell_height(h.size) + 4}, 120, 5});
    }
    for (const Part& part : parts) {
        int64_t lit = 0, lit_ok = 0, shadow = 0, shadow_ok = 0, fill = 0;
        for (int32_t y = part.box.y + 1; y < part.box.bottom() - 1; ++y) {
            for (int32_t x = part.box.x + 1; x < part.box.right() - 1; ++x) {
                const Rgb c = pixel(now, 960, x, y);
                if (c == kFill) ++fill;
                if (c == kLit) {
                    ++lit;
                    lit_ok += pixel(now, 960, x + 1, y + 1) == kFill ? 1 : 0;
                }
                if (c == kShadow) {
                    ++shadow;
                    shadow_ok += pixel(now, 960, x - 1, y - 1) == kFill ? 1 : 0;
                }
            }
        }
        const std::string at = std::string(part.what) + ": ";
        check(fill >= part.fill_at_least && lit >= part.edge_at_least && shadow >= part.edge_at_least, at + "the fill, the lit edge and the shadow edge are all there (" + str(fill) + ", " + str(lit) + ", " + str(shadow) + " pixels)");
        check(lit > 0 && lit_ok * 100 >= lit * 90, at + "the lit edge lies up and to the left of the fill (" + str(lit_ok) + " of " + str(lit) + " pixels have the fill one down and right)");
        check(shadow > 0 && shadow_ok * 100 >= shadow * 90, at + "the shadow edge lies down and to the right of the fill (" + str(shadow_ok) + " of " + str(shadow) + " pixels have the fill one up and left)");
    }
}

// =====================================================================================================================================================
// 4c. The original's page, as before
// =====================================================================================================================================================

// The page of the original's rules (the same result as the 187 pages above), pinned as it was drawn before the 187 lettering got the look of the original's own: the draw calls of both pages (the
// recording renderer: every sprite, rectangle and text, in order) and the pixels of the wide page (FNV-1a 64 over the RGB bytes, the rows' text areas blanked: their pixels depend on the
// machine's font library). A mode that draws a thing of 187's on the original's page, or moves a piece of it, moves these.
constexpr uint64_t kOriginalWideCalls = 0x4ea7cebdf5492e20ull;
constexpr uint64_t kOriginalClassicCalls = 0x95776ac41275aa82ull;
constexpr uint64_t kOriginalWidePixels = 0xcd9f2dab73105aacull;

void test_original_page(const assets::AssetArchive& arc) {
    group("original", "the page of the original's rules is as it was: the same draw calls (both pages) and the same pixels (the wide one)");
    Scene s;
    uint64_t calls[2] = {0, 0};
    for (const bool wide : {true, false}) {
        ScorecardModal card;
        open_page(card, s, sim::GameMode::HighestScore, wide);
        Spy spy(arc);
        card.render(spy, arc);
        calls[wide ? 0 : 1] = spy.fingerprint();
        check(spy.find_text("Kills") == nullptr && spy.find_text("Ants lost") == nullptr && spy.find_text("Ants left") == nullptr, std::string(wide ? "wide" : "classic") + ": no heading of 187 is drawn");
    }
    RendererRig rig(arc, 960, 540);
    check(rig.ok, "the renderer is up on a 960 x 540 canvas");
    uint64_t pixels = 0;
    if (rig.ok) {
        const ResultsLayout& l = ResultsLayout::wide();
        ScorecardModal card;
        open_page(card, s, sim::GameMode::HighestScore, true);
        rig.renderer.begin_frame();
        card.render(rig.renderer, arc);
        const std::vector<uint8_t> px = rig.read();
        std::vector<LayoutRect> masks;
        for (size_t row = 0; row < 4; ++row) masks.push_back(LayoutRect{l.name_x - 2, l.row_y(row) - 2, 925 - (l.name_x - 2), 22});
        Fnv f;
        for (int32_t y = 0; y < 540; ++y) {
            for (int32_t x = 0; x < 960; ++x) {
                bool masked = false;
                for (const LayoutRect& m : masks) masked = masked || m.contains(x, y);
                const Rgb c = pixel(px, 960, x, y);
                f.byte(masked ? uint8_t{0} : c.r);
                f.byte(masked ? uint8_t{0} : c.g);
                f.byte(masked ? uint8_t{0} : c.b);
            }
        }
        pixels = f.value();
    }
    if (g_print) {
        std::printf("constexpr uint64_t kOriginalWideCalls = %s;\nconstexpr uint64_t kOriginalClassicCalls = %s;\nconstexpr uint64_t kOriginalWidePixels = %s;\n", hex64(calls[0]).c_str(), hex64(calls[1]).c_str(), hex64(pixels).c_str());
    }
    check(calls[0] == kOriginalWideCalls, "wide: the draw calls are the original's (" + hex64(calls[0]) + ", pinned " + hex64(kOriginalWideCalls) + ")");
    check(calls[1] == kOriginalClassicCalls, "classic: the draw calls are the original's (" + hex64(calls[1]) + ", pinned " + hex64(kOriginalClassicCalls) + ")");
    check(pixels == kOriginalWidePixels, "wide: the pixels are the original's, the rows' text areas blanked (" + hex64(pixels) + ", pinned " + hex64(kOriginalWidePixels) + ")");
}

// =====================================================================================================================================================
// 5. The headline fits
// =====================================================================================================================================================

void test_fit(const assets::AssetArchive& arc) {
    group("fit", "the headline at the real fonts: every wording lies in its box, a long name makes it smaller or wraps it, a name too long for anything is cut; the page of 4:3 as well");
    RendererRig rig(arc, 960, 540);
    check(rig.ok, "the renderer is up");
    if (!rig.ok) return;
    const std::string long_name = "Bartholomew Maximilian Featherstonehaugh";            // 40 letters: longer than a name of the game
    const std::string very_long = long_name + " and " + long_name + " and " + long_name + " are the last colonies standing!";
    const std::string wordings[] = {
        "Juniper is the last colony standing!",
        "Juniper and Odile are the last colonies standing!",
        "Time is up. The most kills wins!",
        "Nobody is left. The most kills wins!",
        "Bartholomew is the last colony standing!",
        "Bartholomew and Maximilian are the last colonies standing!",
        long_name + " is the last colony standing!",
        long_name + " and " + long_name + " are the last colonies standing!",
        very_long,
    };
    for (const bool wide : {true, false}) {
        const ResultsLayout& l = ResultsLayout::of(wide);
        const std::string page = std::string(wide ? "wide" : "classic") + ": ";
        for (const std::string& text : wordings) {
            const std::string at = page + "\"" + text.substr(0, 30) + "...\": ";
            const Results187Headline h = fit_results_headline_187(rig.renderer, l, text);
            check(!h.lines.empty(), at + "has lines");
            bool in_width = true;
            for (const std::string& line : h.lines) in_width = in_width && rig.renderer.get_text_width(line, h.size) <= l.headline187.w;
            check(in_width, at + "every line is no wider than the box (" + str(l.headline187.w) + ")");
            check(static_cast<int32_t>(h.lines.size()) * font_cell_height(h.size) <= l.headline187.h, at + str(static_cast<int64_t>(h.lines.size())) + " line(s) of " + str(font_cell_height(h.size)) + " px lie in the box's height " + str(l.headline187.h));
            check(h.y >= l.headline187.y && h.y + static_cast<int32_t>(h.lines.size()) * font_cell_height(h.size) <= l.headline187.bottom(), at + "and between its top and bottom");
        }
        // a wording that nothing holds is cut at the smallest size, with "..."
        const Results187Headline cut = fit_results_headline_187(rig.renderer, l, very_long);
        check(cut.size == FontSize::Px18 && !cut.lines.empty() && cut.lines.back().size() >= 3 && cut.lines.back().substr(cut.lines.back().size() - 3) == "...", page + "a wording that nothing holds is cut at the smallest size");
    }
    {   // the approved picture's line: one line at the largest size in the wide page
        const Results187Headline one = fit_results_headline_187(rig.renderer, ResultsLayout::wide(), "Juniper is the last colony standing!");
        check(one.lines.size() == 1 && one.size == FontSize::Px35 && one.lines[0] == "Juniper is the last colony standing!", "wide: the picture's line is one line at the largest size (size " + str(font_cell_height(one.size)) + ", " + str(static_cast<int64_t>(one.lines.size())) + " line(s))");
        check(one.y == kPictureHeadlineY, "wide: and it stands at y 71 (" + str(one.y) + ")");
    }
    // the box of the classic page lies in the free space: right of YOUR SCORE, under the banner, above the headings; the wide page's line between the banner and the headings
    {
        const ResultsLayout& c = ResultsLayout::classic();
        check(c.headline187.x >= c.your_score.right() && c.headline187.y >= c.banner.bottom() && c.headline187.right() <= 640 && c.headline187.bottom() <= c.headings187[2].text_y,
              "classic: the headline's box is right of YOUR SCORE, under the banner, inside the page and above the first heading");
        const ResultsLayout& w = ResultsLayout::wide();
        check(w.headline187.y >= w.banner.bottom() && w.headline187.right() <= 960 && w.headline187.bottom() <= w.headings187[2].text_y, "wide: the headline's box is under the banner, inside the picture and above the first heading");
    }
}

// =====================================================================================================================================================
// 6. The chat log
// =====================================================================================================================================================

/// An engine of the given rules: a 40 x 40 test map, two combat ants of each of the four named teams
sim::SimulationEngine make_engine(sim::GameMode mode) {
    sim::SimulationEngine e;
    e.set_game_mode(mode);
    e.init_test_world(40, 40, 11, 720000);
    for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) e.set_player_eggs(p, 0);
    for (uint8_t t = 0; t < sim::MAX_PLAYERS; ++t) {
        for (int32_t i = 0; i < 2; ++i) e.spawn_unit(t, sim::AntType::Combat, sim::TileCoord{4 + 8 * static_cast<int32_t>(t) + 2 * i, 6});
    }
    e.set_player_name(0, "Juniper");
    e.set_player_name(1, "Odile");
    e.set_player_name(2, "Marta");
    e.set_player_name(3, "Theo");
    return e;
}

void kill_team(sim::SimulationEngine& e, uint8_t team) {
    std::vector<uint32_t> ids;
    for (const sim::AntSnapshot& a : e.get_world_state().ants) {
        if (a.player_id == team) ids.push_back(a.id);
    }
    for (const uint32_t id : ids) e.kill_unit(id);
}

void test_chat() {
    group("chat", "the start line of the chat log in 187 and in the original's rules; \"<Name> (<Colour>) is out of ants!\" reaches the log of every player, in 187 only");
    {
        HUD hud;
        hud.set_game_mode(sim::GameMode::Kills187);
        hud.init(0);
        check(hud.chat_transcript("d @ t") == "d @ t\n\n[0:00] News Flash: Game started! Most kills wins.\n", "187: the first line of the chat log is \"Game started! Most kills wins.\"");
        hud.reset();
        check(hud.chat_transcript("d @ t") == "d @ t\n\n[0:00] News Flash: Game started! Most kills wins.\n", "... after a reset too, once");
        HUD rejoin;
        rejoin.set_game_mode(sim::GameMode::Kills187);
        rejoin.init(0, false);
        check(rejoin.chat_transcript("d @ t") == "d @ t\n\n", "a match that is rejoined has no start line, as in the original's rules");
    }
    {
        HUD hud;
        hud.init(0);
        check(hud.game_mode() == sim::GameMode::HighestScore && hud.chat_transcript("d @ t") == "d @ t\n\n[0:00] News Flash: Game started! Go get that food!\n", "the original's rules (the default): \"Game started! Go get that food!\"");
        HUD back;
        back.set_game_mode(sim::GameMode::Kills187);
        back.set_game_mode(sim::GameMode::HighestScore);
        back.init(0);
        check(back.chat_transcript("d @ t") == "d @ t\n\n[0:00] News Flash: Game started! Go get that food!\n", "... and when the rules are set back");
    }
    {   // a team that has no ant left, no egg and no hatch: every player reads the line once
        for (const uint8_t local : {uint8_t{0}, uint8_t{2}}) {
            sim::SimulationEngine e = make_engine(sim::GameMode::Kills187);
            HUD hud;
            hud.set_game_mode(sim::GameMode::Kills187);
            hud.init(local);
            hud.poll_sim_events(e);
            kill_team(e, 1);
            for (int i = 0; i < 6; ++i) {
                e.tick();
                hud.poll_sim_events(e);
            }
            const std::string log = hud.chat_transcript("d @ t");
            const std::string line = "News Flash: Odile (Red) is out of ants!\n";
            const size_t at = log.find(line);
            check(at != std::string::npos && log.find(line, at + 1) == std::string::npos, "player " + str(local) + " reads \"Odile (Red) is out of ants!\" once");
            check(log.find("Juniper (") == std::string::npos && log.find("Marta (") == std::string::npos && log.find("Theo (") == std::string::npos, "player " + str(local) + ": and nothing of the teams that have ants");
        }
    }
    {   // the original's rules: the same team is lost, and nothing is said
        sim::SimulationEngine e = make_engine(sim::GameMode::HighestScore);
        HUD hud;
        hud.init(0);
        kill_team(e, 1);
        for (int i = 0; i < 6; ++i) {
            e.tick();
            hud.poll_sim_events(e);
        }
        check(hud.chat_transcript("d @ t").find("out of ants") == std::string::npos, "the original's rules: nothing is said of ants that run out");
    }
}

// =====================================================================================================================================================
// 7. The application
// =====================================================================================================================================================

std::filesystem::path temp_ini(const char* stem) {
    static int counter = 0;
    return std::filesystem::temp_directory_path() / (std::string(stem) + "_" + std::to_string(static_cast<unsigned long long>(SDL_GetPerformanceCounter())) + "_" + std::to_string(counter++) + ".ini");
}

struct AppRig {
    explicit AppRig(sim::GameMode mode = sim::GameMode::HighestScore) : settings(temp_ini("ants_results_187")) {
        std::error_code ignore;
        std::filesystem::remove(settings, ignore);
        ApplicationConfig cfg;
        cfg.headless = true;
        cfg.skip_intro = true;
        cfg.aspect = Aspect::Wide16x9;
        cfg.aspect_given = true;
        cfg.start_in_map_select = true;
        cfg.lan_port = 0;
        cfg.settings_path = settings.string();
        cfg.screenshot_path = settings.string() + ".png";                             // (never taken: the run does not stop on its own)
        cfg.screenshot_frames = 1000000000;
        cfg.has_window_size = true;
        cfg.window_w = 960;
        cfg.window_h = 540;
        cfg.game_mode = static_cast<uint8_t>(mode);                                   // (a game of this machine plays by the application's own game mode, as with --game-mode)
        cfg.game_mode_given = mode != sim::GameMode::HighestScore;
        const QuietStdout quiet;
        ok = app.init(cfg);
    }
    ~AppRig() {
        app.shutdown();
        std::error_code ignore;
        std::filesystem::remove(settings, ignore);
    }
    AppRig(const AppRig&) = delete;
    AppRig& operator=(const AppRig&) = delete;

    /// Runs the simulation (50 ms steps; the first hundred are the "Get ready" dialog, in which it waits) until the match is over
    bool play_to_the_end() {
        for (int i = 0; i < 400 && !app.sim().is_match_over(); ++i) app.update_simulation(0.05f);
        return app.sim().is_match_over();
    }

    std::filesystem::path settings;
    Application app;
    bool ok{false};
};

bool chat_has(const HUD& hud, const std::string& text) { return hud.chat_transcript("d @ t").find(text) != std::string::npos; }

void test_application() {
    group("app", "a match of the rules 187 starts with its chat line and ends in its results page; a match of the original's rules does neither");
    {
        AppRig rig(sim::GameMode::Kills187);
        check(rig.ok, "the application is up");
        if (!rig.ok) return;
        check(rig.app.start_game("Original-Ants/Maps/SMALL.LVL"), "a match of the rules 187 starts");
        check(rig.app.sim().game_mode() == sim::GameMode::Kills187 && rig.app.hud().game_mode() == sim::GameMode::Kills187, "the engine and the HUD have the rules");
        check(chat_has(rig.app.hud(), "News Flash: Game started! Most kills wins.\n") && !chat_has(rig.app.hud(), "Go get that food"), "the chat log begins with \"Game started! Most kills wins.\"");
        // a short match: two teams have ants and the clock ends it
        rig.app.sim().init_test_world(60, 60, 5, 600);
        for (uint8_t p = 0; p < sim::MAX_PLAYERS; ++p) rig.app.sim().set_player_eggs(p, 0);
        for (int32_t i = 0; i < 2; ++i) {
            rig.app.sim().spawn_unit(0, sim::AntType::Combat, sim::TileCoord{6 + 2 * i, 6});
            rig.app.sim().spawn_unit(1, sim::AntType::Combat, sim::TileCoord{40 + 2 * i, 40});
        }
        check(rig.play_to_the_end(), "the match ends");
        check(rig.app.scorecard().is_open() && rig.app.scorecard().game_mode() == sim::GameMode::Kills187, "the results page opens, in 187's rules");
        rig.app.update_results(0.3f);
        check(!rig.app.scorecard().is_waiting() && rig.app.scorecard().headline() == "Time is up. The most kills wins!", "and its headline is the clock's: \"" + rig.app.scorecard().headline() + "\"");
        const sim::MatchResult& mr = rig.app.sim().get_world_state().match_result;
        check(mr.ants_left[0] == 2 && mr.ants_left[1] == 2 && mr.ants_left[2] == 0 && mr.ants_left[3] == 0, "the ants left are the engine's (2, 2, 0, 0)");
        const std::vector<ScorecardModal::Row>& rows = rig.app.scorecard().rows();
        check(!rows.empty() && rows[0].numbers[3].empty(), "the rows have three numbers");
    }
    {
        AppRig rig;
        check(rig.ok, "the application is up");
        if (!rig.ok) return;
        check(rig.app.start_game("Original-Ants/Maps/SMALL.LVL"), "a match of the original's rules starts");
        check(rig.app.hud().game_mode() == sim::GameMode::HighestScore && chat_has(rig.app.hud(), "News Flash: Game started! Go get that food!\n"), "its chat log begins as ever");
        rig.app.sim().init_test_world(60, 60, 5, 600);
        check(rig.play_to_the_end(), "the match ends");
        check(rig.app.scorecard().is_open() && rig.app.scorecard().game_mode() == sim::GameMode::HighestScore, "the results page opens, in the original's rules");
        rig.app.update_results(0.3f);
        check(rig.app.scorecard().headline().empty() && !rig.app.scorecard().rows().empty() && !rig.app.scorecard().rows()[0].numbers[3].empty(), "with no headline and the four numbers");
    }
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
    test_headline();
    test_rows();
    test_baked_headings(archive);
    test_positions(archive);
    test_pixels(archive);
    test_original_page(archive);
    test_fit(archive);
    test_chat();
    test_application();
    std::printf("\nresults 187: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
