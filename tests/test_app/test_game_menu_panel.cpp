// The desktop's drawn settings menu (include/ants_app/game_menu_panel.hpp, src/ants_app/game_menu_panel.cpp; docs/GAME_MENU.md): its size and place at every size of the picture, what each press does (a switch, a
// choice, a slider that is dragged, a button, a text field, the tabs, Leave game and its question), the keys, what a click on the dim does, the scroll of the rows, and what it draws with the HUD art and without it.
// It is part of test_game_menu (test_game_menu.cpp calls test_menu_panel); the game side (what the Application answers) is the other cases of that file.
#include <map>

#include "hud_art_fixtures.hpp"

#include "ants_app/game_menu.hpp"
#include "ants_app/game_menu_panel.hpp"
#include "ants_app/window_art.hpp"

namespace gm = ants::app::game_menu;

namespace {

/// A game that records what it is asked
class FakeHost : public GameMenuHost {
public:
    FakeHost() {
        values[gm::kMaster] = 100;
        values[gm::kMusic] = 65;
        values[gm::kEffects] = 99;
        values[gm::kMute] = 0;
        values[gm::kAntHealth] = 0;
        values[gm::kHealthStyle] = 0;
        values[gm::kHealthChanges] = 1;
        values[gm::kShowFps] = 0;
        values[gm::kShowPing] = 0;
        values[gm::kHudArt] = 0;
        values[gm::kScroll] = 50;
        values[gm::kChat] = 1;
        values[gm::kQuickHelpAtStart] = 1;
        texts[0] = "Now you are in for it!";
        texts[1] = "Let me be!";
        texts[2] = "Attack!";
        texts[3] = "Do you want to ally?";
    }
    int get(int what) override {
        if (what == gm::kFactLeave) return leave_kind;
        const auto at = values.find(what);
        return at == values.end() ? -1 : at->second;
    }
    bool set(int id, int value, bool save) override {
        sets.push_back({id, value, save});
        values[id] = value;
        return true;
    }
    std::string text(int id) override { return id >= gm::kQuickChat1 && id <= gm::kQuickChat4 ? texts[static_cast<size_t>(id - gm::kQuickChat1)] : std::string(); }
    void set_text(int id, const std::string& t) override {
        texts[static_cast<size_t>(id - gm::kQuickChat1)] = t;
        ++text_sets;
    }
    bool act(int action) override {
        acts.push_back(action);
        if (action == gm::kActFullscreen) full = !full;
        return true;
    }
    bool fullscreen() override { return full; }
    std::string version() override { return "v9.9.9"; }

    struct Set {
        int id, value;
        bool save;
    };
    std::map<int, int> values;
    std::array<std::string, 4> texts;
    std::vector<Set> sets;
    std::vector<int> acts;
    int leave_kind{static_cast<int>(gm::LeaveKind::EndsMatch)};
    int text_sets{0};
    bool full{false};
};

using Part = GameMenuPanel::Part;
using Item = GameMenuPanel::Item;

const Item* find_item(const std::vector<Item>& list, Part part, int row = -1, int word = -1) {
    for (const Item& it : list) {
        if (it.part == part && (row < 0 || it.row == row) && (word < 0 || it.word == word)) return &it;
    }
    return nullptr;
}

constexpr uint8_t kLeft = 1;                                           // SDL_BUTTON_LEFT

size_t copies_of(const ArtSpy& spy, const hud_art::Art& art, const std::string& name) { return spy.of_sprite(art, name).size(); }

size_t copies_with_prefix(const ArtSpy& spy, const hud_art::Art& art, const std::string& prefix) {
    size_t n = 0;
    for (const hud_art::Sprite& s : art.sprites()) {
        if (s.name.compare(0, prefix.size(), prefix) == 0) n += copies_of(spy, art, s.name);
    }
    return n;
}

LayoutPoint middle(const LayoutRect& r) { return LayoutPoint{r.x + r.w / 2, r.y + r.h / 2}; }

void click(GameMenuPanel& panel, const LayoutRect& r) {
    const LayoutPoint p = middle(r);
    panel.on_mouse_move(p.x, p.y);
    panel.on_mouse_down(p.x, p.y, kLeft);
    panel.on_mouse_up(p.x, p.y, kLeft);
}

struct Rig {
    FakeHost host;
    GameMenuPanel panel{host};
    int closed{0};
    explicit Rig(bool watching = false, int32_t w = 960, int32_t h = 540, double output = 1.0) {
        panel.set_on_close([this]() { ++closed; });
        panel.set_picture(w, h, output);
        panel.open(watching);
    }
    std::vector<Item> items() { return panel.items(); }
    void tab(int index) {
        const std::vector<Item> list = items();
        click(panel, find_item(list, Part::Tab, index)->rect);
    }
};

}  // namespace

int test_menu_panel(int& checks) {
    g_checks = 0;
    g_failures = 0;
    group("panel", "the desktop's drawn settings menu: its place, its presses, its keys and its drawing");
    assets::AssetArchive arc;
    hud_art::Art art;
    const bool have_art = arc.load_chd(std::string(ORIGINAL_ASSETS_DIR) + "/ants.chd") && load_art(art);
    check(have_art, "the archive and the HUD art are there");
    if (!have_art) {
        checks += g_checks;
        return g_failures;
    }

    // ---- the place: whole device pixels to a unit, the panel in the middle of the picture at every size, and shrunk to a picture that is too small
    {
        struct Size {
            int32_t w, h;
            double output;
            int32_t sheet;
            const char* name;
        };
        const Size sizes[] = {{960, 540, 1.0, 1, "960 x 540"}, {960, 540, 2.0, 2, "1920 x 1080"}, {960, 540, 4.0, 3, "3840 x 2160"}, {960, 540, 4.0 / 3.0, 1, "1280 x 720"}, {1200, 540, 1.0, 1, "1200 x 540"},
                              {1260, 540, 2.0, 2, "3440 x 1440 (21:9)"}, {640, 480, 1.0, 1, "640 x 480"}, {640, 480, 2.0, 1, "1280 x 960"}, {390, 844, 3.0, 1, "a phone upright"}};
        for (const Size& s : sizes) {
            const GameMenuScale sc = game_menu_scale(s.w, s.h, s.output);
            const LayoutRect panel = sc.rect(0, 0, kMenuPanelW, kMenuPanelH);
            check(panel.x >= 0 && panel.y >= 0 && panel.right() <= s.w && panel.bottom() <= s.h, std::string(s.name) + ": the panel is inside the picture");
            check(std::abs((panel.x + panel.w / 2) - s.w / 2) <= 1 && std::abs((panel.y + panel.h / 2) - s.h / 2) <= 1, std::string(s.name) + ": ... and in the middle of it");
            const double device_per_unit = sc.f * s.output;
            const bool whole = std::fabs(device_per_unit - std::round(device_per_unit)) < 1e-6;
            if (s.name == std::string("640 x 480") || s.name == std::string("a phone upright")) check(sc.f < 1.0 / s.output + 1e-9 && sc.f > 0.0, std::string(s.name) + ": too small for one device pixel to the unit: the panel shrinks to the picture");
            else check(whole && sc.sheet == s.sheet, std::string(s.name) + ": a unit is " + std::to_string(static_cast<int>(std::lround(device_per_unit))) + " device pixels, drawn from the " + std::to_string(s.sheet) + "x sheet");
        }
    }

    // ---- opening: the sections of a match and of a recording, the first one up
    {
        Rig rig;
        check(rig.panel.is_open() && rig.panel.tab() == 0 && !rig.panel.asking(), "open: the first section is up");
        const std::vector<Item> list = rig.items();
        size_t tabs = 0;
        for (const Item& it : list) tabs += it.part == Part::Tab ? 1u : 0u;
        check(tabs == 5 && find_item(list, Part::Resume) != nullptr && find_item(list, Part::Leave) != nullptr, "a match: five sections, Resume and Leave game");
        Rig watch(true);
        size_t watch_tabs = 0;
        for (const Item& it : watch.items()) watch_tabs += it.part == Part::Tab ? 1u : 0u;
        check(watch_tabs == 3, "a recording: three sections (no Game, no Quick Chat)");
        check(rig.items().size() > 0 && Rig().panel.items().size() == list.size(), "the same walk gives the same places");
        GameMenuPanel closed(rig.host);
        check(closed.items().empty() && !closed.on_key(SDLK_ESCAPE, false) && !closed.on_mouse_down(10, 10, kLeft), "a panel that is closed has no places and takes nothing");
    }

    // ---- the Sound section: sliders. Master volume is heard while it is dragged and kept at the release; the original's two at the release only
    {
        Rig rig;
        const std::vector<Item> list = rig.items();
        const Item* master = find_item(list, Part::Slider, gm::kMaster);
        const Item* music = find_item(list, Part::Slider, gm::kMusic);
        check(master != nullptr && music != nullptr && find_item(list, Part::Word, gm::kMute, 0) != nullptr && find_item(list, Part::Word, gm::kMute, 1) != nullptr, "Sound: three sliders and Mute's two words");
        if (master != nullptr && music != nullptr) {
            // the thumb is 49 units wide and the pointer is its middle: the value 0 is 24 units in from the track's left end, the value 99 is 25 in from its right end
            const double unit = rig.panel.scale().f;
            const auto at_share = [&](const Item* track, double share) { return track->rect.x + static_cast<int32_t>(std::lround(24.0 * unit + share * (track->rect.w - 49.0 * unit))); };
            const LayoutPoint a = middle(master->rect);
            rig.panel.on_mouse_move(a.x, a.y);
            rig.panel.on_mouse_down(a.x, a.y, kLeft);
            check(!rig.host.sets.empty() && rig.host.sets.back().id == gm::kMaster && !rig.host.sets.back().save, "a press on Master volume's track is heard at once, not kept");
            rig.panel.on_mouse_move(at_share(master, 0.25), a.y);
            const int quarter = rig.host.sets.back().value;
            check(quarter >= 20 && quarter <= 30 && !rig.host.sets.back().save, "dragged to the quarter: about 25 of 99, not kept");
            rig.panel.on_mouse_move(master->rect.x - 500, a.y);
            check(rig.host.sets.back().value == 0, "dragged past the left end: 0");
            rig.panel.on_mouse_move(master->rect.right() + 500, a.y);
            check(rig.host.sets.back().value == 99, "dragged past the right end: 99");
            rig.panel.on_mouse_up(at_share(master, 0.5), a.y, kLeft);
            check(rig.host.sets.back().id == gm::kMaster && rig.host.sets.back().save && rig.host.sets.back().value >= 45 && rig.host.sets.back().value <= 54, "the release keeps it where the pointer is: about half");
            rig.host.sets.clear();
            const LayoutPoint m = middle(music->rect);
            rig.panel.on_mouse_down(m.x, m.y, kLeft);
            rig.panel.on_mouse_move(at_share(music, 0.75), m.y);
            check(rig.host.sets.empty(), "the original's Music volume is not heard while it is dragged");
            rig.panel.on_mouse_up(at_share(music, 0.75), m.y, kLeft);
            check(rig.host.sets.size() == 1 && rig.host.sets[0].id == gm::kMusic && rig.host.sets[0].save && rig.host.sets[0].value >= 70 && rig.host.sets[0].value <= 78, "...it is taken at the release, about three quarters");
            rig.host.sets.clear();
            rig.panel.on_mouse_down(m.x, m.y, kLeft);
            rig.panel.cancel_press();
            check(rig.host.sets.size() == 1 && rig.host.sets[0].save, "a drag that the window loses ends where it stands, and keeps that");
        }
        // a switch: a press and a release on the word; a release elsewhere does nothing
        rig.host.sets.clear();
        const std::vector<Item> mute_items = rig.items();
        const Item* off = find_item(mute_items, Part::Word, gm::kMute, 1);
        const Item* on = find_item(mute_items, Part::Word, gm::kMute, 0);
        if (off != nullptr && on != nullptr) {
            const LayoutPoint p = middle(on->rect);
            rig.panel.on_mouse_down(p.x, p.y, kLeft);
            rig.panel.on_mouse_up(p.x - 400, p.y + 100, kLeft);
            check(rig.host.sets.empty(), "a switch: a press and a release away from it do nothing");
            click(rig.panel, on->rect);
            check(rig.host.sets.size() == 1 && rig.host.sets[0].id == gm::kMute && rig.host.sets[0].value == 1 && rig.host.sets[0].save, "...and a click on On is the value 1, kept");
            click(rig.panel, off->rect);
            check(rig.host.sets.back().value == 0, "...a click on Off is the value 0");
        }
        // the right button does nothing
        rig.host.sets.clear();
        if (on != nullptr) {
            const LayoutPoint p = middle(on->rect);
            rig.panel.on_mouse_down(p.x, p.y, 3);
            rig.panel.on_mouse_up(p.x, p.y, 3);
            check(rig.host.sets.empty(), "the right button acts on nothing");
        }
    }

    // ---- the Video section: choices (the word's index), Show FPS and Show ping, and the rows' order
    {
        Rig rig;
        rig.tab(1);
        check(rig.panel.tab() == 1, "a click on Video opens it");
        // the section holds more rows than the well shows: scroll to each row that a test needs
        std::vector<int> seen;
        for (int step = 0; step < 12; ++step) {
            for (const Item& it : rig.items()) {
                if (it.part == Part::Word && std::find(seen.begin(), seen.end(), it.row) == seen.end()) seen.push_back(it.row);
            }
            rig.panel.on_wheel(-1);
        }
        const auto order = [&](int a, int b) { return std::find(seen.begin(), seen.end(), a) < std::find(seen.begin(), seen.end(), b); };
        check(std::find(seen.begin(), seen.end(), gm::kShowFps) != seen.end() && std::find(seen.begin(), seen.end(), gm::kShowPing) != seen.end() && order(gm::kHealthChanges, gm::kShowFps) && order(gm::kShowFps, gm::kShowPing) && order(gm::kShowPing, gm::kHudArt),
              "Video, scrolled: Show FPS, then Show ping, in the table's order");
        check(std::find(seen.begin(), seen.end(), gm::kPicture) == seen.end() && std::find(seen.begin(), seen.end(), gm::kLockMouse) == seen.end(), "the browser's rows (the picture's shape, the fullscreen mouse) are not in the desktop's panel");
        // a choice: Hurt ants, All ants, Off: the word's index
        rig.panel.on_wheel(100);
        rig.host.sets.clear();
        const std::vector<Item> list = rig.items();
        const Item* all = find_item(list, Part::Word, gm::kAntHealth, 1);
        if (all == nullptr) {
            rig.panel.on_wheel(-1);
        }
        for (int guard = 0; guard < 12 && find_item(rig.items(), Part::Word, gm::kAntHealth, 2) == nullptr; ++guard) rig.panel.on_wheel(-1);
        const std::vector<Item> health_items = rig.items();
        const Item* off = find_item(health_items, Part::Word, gm::kAntHealth, 2);
        check(off != nullptr, "Ant health has its three words");
        if (off != nullptr) {
            click(rig.panel, off->rect);
            check(rig.host.sets.size() == 1 && rig.host.sets[0].id == gm::kAntHealth && rig.host.sets[0].value == 2 && rig.host.sets[0].save, "a click on the third word is the value 2");
        }
        // Show FPS: On is the value 1
        rig.host.sets.clear();
        for (int guard = 0; guard < 24 && find_item(rig.items(), Part::Word, gm::kShowFps, 0) == nullptr; ++guard) rig.panel.on_wheel(guard < 12 ? 1 : -1);
        const std::vector<Item> fps_items = rig.items();
        const Item* fps_on = find_item(fps_items, Part::Word, gm::kShowFps, 0);
        check(fps_on != nullptr, "Show FPS has its On and Off");
        if (fps_on != nullptr) {
            click(rig.panel, fps_on->rect);
            check(rig.host.sets.size() == 1 && rig.host.sets[0].id == gm::kShowFps && rig.host.sets[0].value == 1, "a click on Show FPS's On is the value 1");
        }
    }

    // ---- the Controls list, the Game section's buttons and its Version
    {
        Rig rig;
        rig.tab(2);
        check(find_item(rig.items(), Part::Slider) == nullptr && find_item(rig.items(), Part::Word) == nullptr && rig.panel.tab() == 2, "Controls: a list to read, nothing to press but the tabs");
        rig.tab(3);
        const std::vector<Item> list = rig.items();
        check(find_item(list, Part::Word, gm::kChat, 0) != nullptr && find_item(list, Part::Word, gm::kQuickHelpAtStart, 1) != nullptr && find_item(list, Part::Button, gm::kQuickHelp) != nullptr && find_item(list, Part::Button, gm::kFullscreen) == nullptr,
              "Game: the two switches and the Quick help button (Fullscreen is in Video)");
        click(rig.panel, find_item(list, Part::Button, gm::kQuickHelp)->rect);
        check(!rig.panel.is_open() && rig.closed == 1 && rig.host.acts == std::vector<int>{gm::kActQuickHelp}, "Quick help closes the menu and asks the game for the original's quick help");
        Rig video;
        video.tab(1);
        const std::vector<Item> video_items = video.items();
        const Item* fullscreen = find_item(video_items, Part::Button, gm::kFullscreen);
        check(fullscreen != nullptr, "Video: a Fullscreen button");
        if (fullscreen != nullptr) {
            click(video.panel, fullscreen->rect);
            check(video.panel.is_open() && video.host.acts == std::vector<int>{gm::kActFullscreen} && video.host.full, "Fullscreen is the game's act, and the menu stays up");
            ArtSpy spy(arc);
            spy.face_ready = true;
            spy.dialog_face = true;
            video.panel.render(spy, &art, 0);
            bool leave_word = false, enter_word = false;
            for (const auto& f : spy.lines) {
                leave_word = leave_word || f.text == "Leave fullscreen";
                enter_word = enter_word || f.text == "Enter fullscreen";
            }
            check(leave_word && !enter_word, "...and the button says what it does now: Leave fullscreen");
        }
    }

    // ---- the Quick Chat section: four fields, the focus, the typing
    {
        Rig rig;
        rig.tab(4);
        check(rig.panel.tab() == 4, "a click on Quick Chat opens it");
        const std::vector<Item> list = rig.items();
        const Item* f9 = find_item(list, Part::Edit, gm::kQuickChat1);
        const Item* f12 = find_item(list, Part::Edit, gm::kQuickChat4);
        check(f9 != nullptr && f12 != nullptr && find_item(list, Part::Edit, gm::kQuickChat1 + 1) != nullptr && find_item(list, Part::Edit, gm::kQuickChat1 + 2) != nullptr, "four fields");
        if (f9 != nullptr && f12 != nullptr) {
            check(rig.panel.focused_text() < 0, "none has the focus when the section opens");
            rig.panel.on_text("x");
            check(rig.host.text_sets == 0, "text with no focus goes nowhere");
            click(rig.panel, f9->rect);
            check(rig.panel.focused_text() == gm::kQuickChat1, "a click on a field gives it the focus");
            rig.panel.on_text(" all");
            check(rig.host.texts[0] == "Now you are in for it! all" && rig.host.text_sets == 1, "typed letters are added, and kept at once");
            rig.panel.on_key(SDLK_BACKSPACE, false);
            check(rig.host.texts[0] == "Now you are in for it! al", "Backspace takes one off");
            rig.panel.on_text("\xC3\xA9 \x01!");
            check(rig.host.texts[0] == "Now you are in for it! al !", "only printable ASCII is taken (an accent and a control character are left out)");
            rig.host.texts[0] = std::string(99, 'a');
            rig.panel.on_text("bcd");
            check(rig.host.texts[0] == std::string(99, 'a') + "b", "a text is held to 100 letters");
            rig.panel.on_key(SDLK_TAB, false);
            check(rig.panel.focused_text() == gm::kQuickChat1 + 1, "Tab goes to the next field");
            rig.panel.on_key(SDLK_TAB, false);
            rig.panel.on_key(SDLK_TAB, false);
            rig.panel.on_key(SDLK_TAB, false);
            check(rig.panel.focused_text() == gm::kQuickChat1, "...and round to the first");
            rig.panel.on_key(SDLK_RETURN, false);
            check(rig.panel.focused_text() < 0 && rig.panel.is_open(), "Enter lets the field go and keeps the menu");
            click(rig.panel, f12->rect);
            rig.panel.on_key(SDLK_ESCAPE, false);
            check(!rig.panel.is_open() && rig.closed == 1, "Esc closes the menu, a field that has the focus or not");
        }
        // a field that is pressed and then another section: the focus goes
        Rig other;
        other.tab(4);
        click(other.panel, find_item(other.items(), Part::Edit, gm::kQuickChat1 + 2)->rect);
        other.tab(0);
        check(other.panel.focused_text() < 0, "another section: no field has the focus");
    }

    // ---- the keys: Esc closes (not a held one), Enter is Resume, Up and Down walk the sections
    {
        Rig rig;
        rig.panel.on_key(SDLK_DOWN, false);
        rig.panel.on_key(SDLK_DOWN, false);
        check(rig.panel.tab() == 2, "Down moves to the next section");
        rig.panel.on_key(SDLK_UP, false);
        rig.panel.on_key(SDLK_UP, false);
        rig.panel.on_key(SDLK_UP, false);
        check(rig.panel.tab() == 0, "Up moves back, and stops at the first");
        for (int i = 0; i < 9; ++i) rig.panel.on_key(SDLK_DOWN, false);
        check(rig.panel.tab() == 4, "...and Down at the last");
        rig.panel.on_key(SDLK_ESCAPE, true);
        check(rig.panel.is_open(), "a held Esc (a repeat) does not close it");
        rig.panel.on_key(SDLK_ESCAPE, false);
        check(!rig.panel.is_open() && rig.closed == 1, "a first Esc does");
        Rig enter;
        enter.panel.on_key(SDLK_RETURN, true);
        check(enter.panel.is_open(), "a held Enter does nothing");
        enter.panel.on_key(SDLK_RETURN, false);
        check(!enter.panel.is_open(), "Enter is Resume, the default button");
        Rig again;
        again.panel.on_key(SDLK_ESCAPE, false);
        again.panel.open(false);
        check(again.panel.is_open() && again.panel.tab() == 0 && again.closed == 1, "opened again it is on its first section");
    }

    // ---- a click on the dim closes it, a drag from inside the panel to the dim does not, Resume closes
    {
        Rig rig;
        const LayoutRect panel = rig.panel.panel_rect();
        rig.panel.on_mouse_down(panel.x + 5, panel.y + 5, kLeft);
        rig.panel.on_mouse_up(panel.x - 20, panel.y - 20, kLeft);
        check(rig.panel.is_open(), "a press inside the panel and a release on the dim leave it up");
        rig.panel.on_mouse_down(panel.x - 20, panel.y - 20, kLeft);
        rig.panel.on_mouse_up(panel.x + 5, panel.y + 5, kLeft);
        check(rig.panel.is_open(), "a press on the dim and a release inside it too");
        rig.panel.on_mouse_down(panel.x - 20, panel.y - 20, kLeft);
        rig.panel.on_mouse_up(panel.x - 20, panel.y - 20, kLeft);
        check(!rig.panel.is_open() && rig.closed == 1, "a press and a release on the dim close it");
        Rig resume;
        click(resume.panel, find_item(resume.items(), Part::Resume)->rect);
        check(!resume.panel.is_open() && resume.host.acts.empty(), "Resume closes it and asks nothing of the game");
    }

    // ---- Leave game: the question of the kind that asks, Stay by default (Enter, Esc), the way out; a recording asks nothing
    {
        for (const gm::LeaveKind kind : {gm::LeaveKind::EndsMatch, gm::LeaveKind::DropOut, gm::LeaveKind::Forfeit, gm::LeaveKind::Held}) {
            Rig rig;
            rig.host.leave_kind = static_cast<int>(kind);
            click(rig.panel, find_item(rig.items(), Part::Leave)->rect);
            check(rig.panel.asking() && rig.panel.is_open() && rig.host.acts.empty(), "Leave game asks first (kind " + std::to_string(static_cast<int>(kind)) + ")");
            const std::vector<Item> q = rig.items();
            check(find_item(q, Part::Stay) != nullptr && find_item(q, Part::Go) != nullptr && find_item(q, Part::Tab) == nullptr && find_item(q, Part::Leave) == nullptr, "...the question has Stay and the way out, and nothing under it takes a click");
            rig.panel.on_key(SDLK_ESCAPE, false);
            check(!rig.panel.asking() && rig.panel.is_open(), "Esc answers Stay: the menu is still there");
            click(rig.panel, find_item(rig.items(), Part::Leave)->rect);
            rig.panel.on_key(SDLK_RETURN, false);
            check(!rig.panel.asking() && rig.panel.is_open(), "Enter answers Stay too");
            click(rig.panel, find_item(rig.items(), Part::Leave)->rect);
            click(rig.panel, find_item(rig.items(), Part::Stay)->rect);
            check(!rig.panel.asking() && rig.host.acts.empty(), "Stay stays");
            click(rig.panel, find_item(rig.items(), Part::Leave)->rect);
            click(rig.panel, find_item(rig.items(), Part::Go)->rect);
            check(!rig.panel.is_open() && rig.host.acts == std::vector<int>{gm::kActLeave}, "the way out closes the menu and is Leave game for the game");
        }
        Rig watch(true);
        watch.host.leave_kind = static_cast<int>(gm::LeaveKind::Watch);
        click(watch.panel, find_item(watch.items(), Part::Leave)->rect);
        check(!watch.panel.is_open() && watch.host.acts == std::vector<int>{gm::kActLeave}, "a recording asks nothing: Leave game leaves");
        Rig dim;
        dim.host.leave_kind = static_cast<int>(gm::LeaveKind::DropOut);
        click(dim.panel, find_item(dim.items(), Part::Leave)->rect);
        const LayoutRect panel = dim.panel.panel_rect();
        dim.panel.on_mouse_down(panel.x - 10, panel.y - 10, kLeft);
        dim.panel.on_mouse_up(panel.x - 10, panel.y - 10, kLeft);
        check(dim.panel.is_open() && dim.panel.asking(), "a click on the dim does not answer the question");
    }

    // ---- the scroll: the rows that the well shows, the rows that it does not are not clickable
    {
        Rig rig;
        rig.tab(1);
        const std::vector<Item> top = rig.items();
        const int rows_in_view = static_cast<int>(std::count_if(top.begin(), top.end(), [](const Item& it) { return it.part == Part::Word || it.part == Part::Button || it.part == Part::Slider; }));
        check(rows_in_view > 0 && find_item(top, Part::Word, gm::kHudArt) == nullptr, "Video: the first rows are in the well, the last one is below it (not a place to click)");
        rig.panel.on_wheel(1);
        check(rig.panel.scroll() == 0, "a wheel up at the top stays at the top");
        for (int i = 0; i < 20; ++i) rig.panel.on_wheel(-1);
        const int bottom = rig.panel.scroll();
        check(bottom > 0 && find_item(rig.items(), Part::Word, gm::kHudArt, 0) != nullptr, "scrolled to the end: the last row is there");
        rig.panel.on_wheel(-1);
        check(rig.panel.scroll() == bottom, "...and no further");
        rig.tab(0);
        check(rig.panel.scroll() == 0, "another section starts at its top");
        // every place is inside the panel, and a row's places are inside the well
        bool inside = true;
        const LayoutRect panel = rig.panel.panel_rect();
        for (int t = 0; t < 5; ++t) {
            rig.tab(t);
            for (int s = 0; s < 14; ++s) {
                for (const Item& it : rig.items()) inside = inside && it.rect.x >= panel.x && it.rect.y >= panel.y && it.rect.right() <= panel.right() && it.rect.bottom() <= panel.bottom() && it.rect.w > 0 && it.rect.h > 0;
                rig.panel.on_wheel(-1);
            }
        }
        check(inside, "every place of every section, at every scroll, is inside the panel");
    }

    // ---- the drawing
    {
        for (const double output : {1.0, 2.0}) {
            Rig rig;
            ArtSpy spy(arc);
            spy.output = output;
            spy.face_ready = true;
            spy.dialog_face = true;
            rig.panel.set_picture(960, 540, output);
            rig.panel.render(spy, &art, 100);
            const std::string at = std::string(output == 1.0 ? "1x" : "2x") + ": ";
            check(!spy.copies.empty() && copies_with_prefix(spy, art, "panel_") > 0 && copies_with_prefix(spy, art, "sc_well_") > 0 && copies_with_prefix(spy, art, "sc_sm_") > 0 && copies_with_prefix(spy, art, "op_thumb_") == 3, at + "drawn with the HUD art: the panel, the well, the steel buttons, three thumbs");
            check(copies_of(spy, art, "op_on_up") + copies_of(spy, art, "op_on_chosen") == 1 && copies_of(spy, art, "op_off_chosen") == 1, at + "Mute: its On and Off, Off chosen");
            bool sheets_ok = spy.sheets.size() >= 2 && spy.sheets.front() == game_menu_scale(960, 540, output).sheet && spy.sheets.back() == 0;
            check(sheets_ok, at + "the sheet of the unit is chosen for the panel, and the choice is given back");
            const LayoutRect panel = rig.panel.panel_rect();
            bool texts_in = !spy.lines.empty();
            for (const auto& f : spy.lines) texts_in = texts_in && f.x >= panel.x - 2 && f.x <= panel.right();
            check(texts_in, at + "every line of the windows' face starts inside the panel");
            bool title = false, resume = false;
            for (const auto& f : spy.lines) {
                title = title || f.text == "Settings";
                resume = resume || f.text == "Resume";
            }
            check(title && resume, at + "the title and Resume are in the windows' face");
        }
        {   // plain rectangles without the art: no sprite of it, and the panel is still there
            Rig rig;
            ArtSpy spy(arc);
            rig.panel.render(spy, nullptr, 0);
            check(spy.copies.empty() && spy.count(Spy::Kind::Fill) > 10 && spy.count(Spy::Kind::Text) > 5, "without the art: plain rectangles and the lines in the original's face, no copy of the art");
        }
        {   // a closed panel draws nothing; an open one draws the dim over the whole picture
            Rig rig;
            ArtSpy spy(arc);
            rig.panel.close();
            rig.panel.render(spy, &art, 0);
            check(spy.events.empty() && spy.copies.empty(), "a closed panel draws nothing");
            rig.panel.open(false);
            ArtSpy open_spy(arc);
            rig.panel.render(open_spy, &art, 0);
            const auto fills = open_spy.of(Spy::Kind::Fill);
            check(!fills.empty() && fills[0].x == 0 && fills[0].y == 0 && fills[0].w == 960 && fills[0].h == 540 && fills[0].color.a > 100 && fills[0].color.a < 200, "the dim is the first thing drawn, over the whole picture");
        }
        {   // the pane is clipped: a row that the scroll puts half out of the well is cut at its edge, and the clip is given back
            Rig rig;
            rig.tab(1);
            rig.panel.on_wheel(-1);
            ArtSpy spy(arc);
            rig.panel.render(spy, &art, 0);
            check(spy.count(Spy::Kind::Clip) == 1 && spy.count(Spy::Kind::ClearClip) == 1, "the rows are drawn inside one clip of the well");
        }
        {   // the question draws over everything and takes the footer's places away
            Rig rig;
            rig.host.leave_kind = static_cast<int>(gm::LeaveKind::Forfeit);
            click(rig.panel, find_item(rig.items(), Part::Leave)->rect);
            ArtSpy spy(arc);
            spy.face_ready = true;
            spy.dialog_face = true;
            rig.panel.render(spy, &art, 0);
            bool title = false, go = false;
            for (const auto& f : spy.lines) {
                title = title || f.text == "Forfeit and leave?";
                go = go || f.text == "Forfeit";
            }
            check(title && go, "the question says what the game says: \"Forfeit and leave?\" and Forfeit");
        }
        {   // the version row
            Rig rig;
            rig.tab(3);
            ArtSpy spy(arc);
            rig.panel.render(spy, &art, 0);
            bool version = false;
            for (const Spy::Ev& e : spy.of(Spy::Kind::Text)) version = version || e.name == "v9.9.9";
            check(version, "the Game section shows the version that the game says");
        }
    }
    checks += g_checks;
    return g_failures;
}
